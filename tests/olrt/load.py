"""Background DML load for connect scenarios: concurrent sessions that keep committing while the
steps (adapter swap, rewind, restarts) run, so a switch lands in the middle of real traffic.

Every worker owns its keys (inserted rows plus a modulo slice of the rows that exist at start), so
sessions never wait on each other's row locks. Shapes, all mixed into one run:

  short      1-`max_ops` DML across the tables, think time, COMMIT; some ROLLBACK TO SAVEPOINT, some ROLLBACK
  long       the same, but held open `long_hold_s` and touched now and then: open across a switch
  batch      one transaction with `rows` inserts + updates on one table, begun and committed by steps
  switcher   ALTER SYSTEM ARCHIVE LOG CURRENT every `switch_s`, so transactions span log switches

Column values are generated from ALL_TAB_COLUMNS (NUMBER with its scale, VARCHAR2 up to its length,
DATE), so the wide tables are written across all their columns.
"""
import datetime
import random
import threading
import time

from . import db


class _Table:
    def __init__(self, conn, name, key):
        owner, table = name.split(".")
        self.name, self.key = name, list(key)
        cur = conn.cursor()
        cur.execute("""SELECT column_name, data_type, data_length, data_precision, data_scale
                         FROM all_tab_cols
                        WHERE owner = :o AND table_name = :t AND column_id IS NOT NULL
                          AND virtual_column = 'NO' AND hidden_column = 'NO'
                        ORDER BY column_id""", o=owner, t=table)
        self.cols = [dict(name=r[0], type=r[1], length=int(r[2] or 0),
                          precision=int(r[3]) if r[3] is not None else None,
                          scale=int(r[4]) if r[4] is not None else None) for r in cur]
        if not self.cols:
            raise RuntimeError(f"load: no columns for {name}")
        self.data = [c for c in self.cols if c["name"] not in self.key]
        names = [c["name"] for c in self.cols]
        self.insert_sql = (f"INSERT INTO {name} ({', '.join(names)}) VALUES "
                           f"({', '.join(':' + str(i + 1) for i in range(len(names)))})")
        cur.execute(f"SELECT {', '.join(self.key)} FROM {name}")
        self.existing = [tuple(r) for r in cur]

    def value(self, rnd, c):
        t = c["type"]
        if t == "NUMBER":
            p = c["precision"] or 15
            s = c["scale"] or 0
            whole = 10 ** max(0, min(p - s, 9)) - 1
            if s:
                # trailing zeros on purpose: scale must survive (0.000, 12.500)
                return round(rnd.choice([0, rnd.uniform(0, whole), rnd.randint(0, 99) + 0.5]), s)
            return rnd.randint(0, whole)
        if t == "DATE":
            return datetime.datetime(2020, 1, 1) + datetime.timedelta(seconds=rnd.randint(0, 6 * 365 * 86400))
        if t.startswith("VARCHAR"):
            n = rnd.randint(0, min(c["length"] or 1, 24))
            return None if n == 0 else "".join(rnd.choice("ABCDEFGHJKLMNPQRSTUVWXYZ0123456789 ") for _ in range(n)).strip() or "X"
        return None

    def row(self, rnd, key):
        vals = dict(zip(self.key, key))
        return [vals[c["name"]] if c["name"] in vals else self.value(rnd, c) for c in self.cols]

    def update_sql(self, rnd):
        cols = rnd.sample(self.data, k=min(len(self.data), rnd.randint(1, 5)))
        sets = ", ".join(f"{c['name']} = :v{i}" for i, c in enumerate(cols))
        where = " AND ".join(f"{k} = :k{i}" for i, k in enumerate(self.key))
        return f"UPDATE {self.name} SET {sets} WHERE {where}", cols

    def delete_sql(self):
        return f"DELETE FROM {self.name} WHERE " + " AND ".join(f"{k} = :k{i}" for i, k in enumerate(self.key))


class Load:
    def __init__(self, tables, keys, opts, seed=None, log=None):
        """tables: OWNER.TABLE list; keys: OWNER.TABLE -> key columns (PK when absent)."""
        self.opts = dict(sessions=6, long_sessions=2, max_ops=20, think_ms=150, savepoint_pct=10,
                         rollback_pct=5, long_hold_s=[20, 90], switch_s=20, key_base=10**8)
        self.opts.update(opts or {})
        self.seed = seed if seed is not None else random.randrange(10**9)
        self.log = log or (lambda m: None)
        self.stop_ev = threading.Event()
        self.threads, self.errors = [], []
        self.stats = {"commits": 0, "rollbacks": 0, "savepoint_rollbacks": 0, "rows": 0, "switches": 0}
        self.lock = threading.Lock()
        conn = db.pdb()
        try:
            self.tables = [_Table(conn, t, keys.get(t) or _pk(conn, t)) for t in tables]
        finally:
            conn.close()
        self.batch = None

    def _count(self, **kw):
        with self.lock:
            for k, v in kw.items():
                self.stats[k] += v

    def start(self):
        n = int(self.opts["sessions"]) + int(self.opts["long_sessions"])
        for w in range(n):
            longw = w >= int(self.opts["sessions"])
            th = threading.Thread(target=self._guard, args=(self._worker, w, n, longw), daemon=True,
                                  name=f"load-{'long' if longw else 'short'}-{w}")
            th.start()
            self.threads.append(th)
        if float(self.opts["switch_s"]) > 0:
            th = threading.Thread(target=self._guard, args=(self._switcher,), daemon=True, name="load-switch")
            th.start()
            self.threads.append(th)
        self.log(f"load: {n} sessions, seed {self.seed}, opts {self.opts}")

    def stop(self, timeout=180):
        """Stop the workers; each commits what it holds, so nothing is left open at the end SCN."""
        self.stop_ev.set()
        for th in self.threads:
            th.join(timeout)
        alive = [th.name for th in self.threads if th.is_alive()]
        if alive:
            self.errors.append(f"load threads still running after {timeout} s: {alive}")
        self.log(f"load stopped: {self.stats}, errors {len(self.errors)}")
        return dict(self.stats, seed=self.seed, errors=self.errors)

    def _guard(self, fn, *args):
        try:
            fn(*args)
        except Exception as e:  # noqa: BLE001 - reported, fails the run
            self.errors.append(f"{threading.current_thread().name}: {type(e).__name__}: {e}")

    def _switcher(self):
        conn = db.root()
        try:
            while not self.stop_ev.wait(float(self.opts["switch_s"])):
                db.switch_logfile(conn)
                self._count(switches=1)
        finally:
            conn.close()

    def _worker(self, w, n, longw):
        rnd = random.Random(self.seed * 1000 + w)
        conn = db.pdb()
        cur = conn.cursor()
        # keys this worker may touch: own slice of the existing rows + rows it inserted
        live = {t.name: [k for i, k in enumerate(t.existing) if i % n == w] for t in self.tables}
        next_id = int(self.opts["key_base"]) + w * 10**6
        think = float(self.opts["think_ms"]) / 1000
        try:
            while not self.stop_ev.is_set():
                pending = {t: list(v) for t, v in live.items()}   # committed state to restore on ROLLBACK
                hold_until = time.time() + (rnd.uniform(*self.opts["long_hold_s"]) if longw else 0)
                ops = rnd.randint(1, int(self.opts["max_ops"]))
                sp = rnd.randrange(100) < int(self.opts["savepoint_pct"])
                sp_at, sp_state = (rnd.randint(0, ops - 1) if sp else -1), None
                rows = 0
                for i in range(ops):
                    if i == sp_at:
                        cur.execute("SAVEPOINT sp1")
                        sp_state = {t: list(v) for t, v in live.items()}
                    next_id = self._op(cur, rnd, live, next_id)
                    rows += 1
                if sp_state is not None and rnd.random() < 0.7:
                    cur.execute("ROLLBACK TO SAVEPOINT sp1")
                    live = sp_state
                    self._count(savepoint_rollbacks=1)
                while longw and time.time() < hold_until and not self.stop_ev.is_set():
                    if self.stop_ev.wait(rnd.uniform(2, 8)):
                        break
                    next_id = self._op(cur, rnd, live, next_id)
                    rows += 1
                if rnd.randrange(100) < int(self.opts["rollback_pct"]):
                    conn.rollback()
                    live = pending
                    self._count(rollbacks=1)
                else:
                    conn.commit()
                    self._count(commits=1, rows=rows)
                if think:
                    self.stop_ev.wait(rnd.uniform(0, 2 * think))
            conn.commit()
        finally:
            try:
                conn.commit()
                conn.close()
            except Exception:
                pass

    def _op(self, cur, rnd, live, next_id):
        t = rnd.choice(self.tables)
        keys = live[t.name]
        r = rnd.random()
        if not keys or r < 0.4:
            key = _new_key(t, next_id)
            cur.execute(t.insert_sql, t.row(rnd, key))
            keys.append(key)
            return next_id + 1
        if r < 0.85:
            k = rnd.choice(keys)
            sql, cols = t.update_sql(rnd)
            b = {f"v{i}": t.value(rnd, c) for i, c in enumerate(cols)}
            b.update({f"k{i}": v for i, v in enumerate(k)})
            cur.execute(sql, b)
            return next_id
        k = keys.pop(rnd.randrange(len(keys)))
        cur.execute(t.delete_sql(), {f"k{i}": v for i, v in enumerate(k)})
        return next_id

    # ---- one large transaction, begun and committed by steps (open across a switch)

    def batch_begin(self, table, rows, chunk=5000):
        t = next(x for x in self.tables if x.name == table)
        rnd = random.Random(self.seed + 7)
        conn = db.pdb()
        cur = conn.cursor()
        base = int(self.opts["key_base"]) + 900 * 10**6
        keys = [_new_key(t, base + i) for i in range(int(rows))]
        for i in range(0, len(keys), chunk):
            cur.executemany(t.insert_sql, [t.row(rnd, k) for k in keys[i:i + chunk]])
        # and an update of every row in the same transaction, as a bulk job would
        col = next(c for c in t.data if c["type"] == "NUMBER")
        where = " AND ".join(f"{k} = :{i + 2}" for i, k in enumerate(t.key))
        sql = f"UPDATE {t.name} SET {col['name']} = :1 WHERE {where}"
        for i in range(0, len(keys), chunk):
            cur.executemany(sql, [[t.value(rnd, col), *k] for k in keys[i:i + chunk]])
        self.batch = conn
        self.log(f"load: batch on {table} open with {2 * len(keys)} rows")
        return 2 * len(keys)

    def batch_commit(self):
        if self.batch is not None:
            self.batch.commit()
            self.batch.close()
            self.batch = None


def _pk(conn, name):
    owner, table = name.split(".")
    cur = conn.cursor()
    cur.execute("""SELECT cc.column_name FROM all_constraints c
                     JOIN all_cons_columns cc ON cc.owner = c.owner AND cc.constraint_name = c.constraint_name
                    WHERE c.owner = :o AND c.table_name = :t AND c.constraint_type = 'P'
                    ORDER BY cc.position""", o=owner, t=table)
    cols = [r[0] for r in cur]
    if not cols:
        raise RuntimeError(f"load: {name} has no primary key and no key in scenario.toml")
    return cols


def _new_key(t, n):
    # first key column carries the unique number, the rest constant (fits any composite key)
    types = {c["name"]: c["type"] for c in t.cols}
    vals = [n] + [1] * (len(t.key) - 1)
    return tuple(str(v) if types[k].startswith("VARCHAR") else v for k, v in zip(t.key, vals))
