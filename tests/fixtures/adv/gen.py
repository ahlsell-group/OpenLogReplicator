#!/usr/bin/env python3
"""Generates the adversarial width/random scenarios under scenarios/adv/.

  python3 fixtures/adv/gen.py            # regenerate all, commit the result

Tables have N columns in total (ID NUMBER(10) primary key + N-1 data columns cycling
VARCHAR2(100) / NUMBER(15,3) / DATE / NUMBER), all with SUPPLEMENTAL LOG DATA (ALL) COLUMNS,
as usual for a CDC source. Widths sit on both sides of OLR's 64-column bitmap words (valuesSet,
Builder.h) and Oracle's 255-column row-piece limit: 63/64/65, 127/128/129, 255/256/257.

Two kinds of scenario:

  boundary   a fixed sequence aimed at per-DML builder state leaks (the ERROR 50073 class):
             a DELETE of a row whose last non-NULL column sits just below a word boundary,
             followed by DML on a table of another width that touches the next word; all-NULL
             rows, trailing NULLs, high-column-only updates, PK-only deletes, rows that grow
             until they migrate/chain inside the transaction.
  random     a seeded random DML mix (reproducible: same seed -> same SQL) over a few widths,
             several sessions with interleaved transactions committing out of begin order,
             savepoints, rollbacks, multi-row statements, log switches mid-transaction.
             Sessions own disjoint keys (id mod sessions), so nothing ever blocks on a lock.
"""
import datetime as dt
import pathlib
import random

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = ROOT / "scenarios" / "adv"

VLEN = 100


def col_name(i):
    return "ID" if i == 1 else f"C{i}"


def col_type(i):
    if i == 1:
        return "NUMBER(10)"
    return {2: "VARCHAR2(100)", 3: "NUMBER(15,3)", 0: "DATE", 1: "NUMBER"}[i % 4]


def kind(i):
    return {2: "v", 3: "n", 0: "d", 1: "b"}[i % 4] if i > 1 else "k"


# ------------------------------------------------------------------ values

NUM_EDGES = ["0", "0.000", "-0", "1", "-1", "0.001", "-0.001", "999999999999.999", "-999999999999.999",
             "0.5", "-0.5", "100", "123456789.123"]
BARE_EDGES = ["0", "-0", "1e125", "-1e125", "9.9999999999999999999999999999999999999e125",
              "99999999999999999999999999999999999999", "-99999999999999999999999999999999999999",
              "0.1", "1e-120", "-1e-120", "3.14159265358979323846264338327950288", "12345678901234567890.5"]
DATE_EDGES = ["0001-01-01 00:00:00", "9999-12-31 23:59:59", "1970-01-01 00:00:00", "1969-12-31 23:59:59",
              "2000-02-29 12:00:00", "1582-10-15 00:00:00", "1899-12-31 23:59:59", "2038-01-19 03:14:08",
              "2026-03-29 02:30:00", "2026-10-25 02:30:00"]


def lit(rng, k, edge_bias=0.3):
    if k == "v":
        r = rng.random()
        if r < 0.05:
            return "''"                    # '' is NULL in Oracle
        if r < 0.1:
            return "' '"
        n = rng.choice([1, 2, 7, 30, 99, 100])
        alphabet = "abcXYZ019 -_'"
        s = "".join(rng.choice(alphabet) for _ in range(n))
        return "'" + s.replace("'", "''") + "'"
    if k == "n":
        if rng.random() < edge_bias:
            return rng.choice(NUM_EDGES)
        return f"{rng.randint(-10**12 + 1, 10**12 - 1)}.{rng.randint(0, 999):03d}"
    if k == "b":
        if rng.random() < edge_bias:
            return rng.choice(BARE_EDGES)
        return str(rng.randint(-10**18, 10**18)) + rng.choice(["", ".25", "e10", "e-5"])
    if k == "d":
        if rng.random() < edge_bias:
            s = rng.choice(DATE_EDGES)
        else:
            d = dt.datetime(1, 1, 1) + dt.timedelta(seconds=rng.randint(0, 315_537_897_599))
            s = f"{d.year:04d}-{d.month:02d}-{d.day:02d} {d.hour:02d}:{d.minute:02d}:{d.second:02d}"
        return f"TO_DATE('{s}','YYYY-MM-DD HH24:MI:SS')"
    raise ValueError(k)


def long_val(i):
    """Value that makes a column as wide as it gets (row growth -> migration/chaining)."""
    k = kind(i)
    if k == "v":
        return f"RPAD('g{i}', {VLEN}, 'x')"
    if k in ("n", "b"):
        return "-999999999999.999" if k == "n" else "-1.2345678901234567890123456789012345678e-100"
    return "TO_DATE('9999-12-31 23:59:59','YYYY-MM-DD HH24:MI:SS')"


# ------------------------------------------------------------------ SQL builders

def ddl(schema, widths, pctfree=None):
    out = [f"BEGIN EXECUTE IMMEDIATE 'DROP USER {schema} CASCADE';",
           "EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1918 THEN RAISE; END IF; END;", "/",
           f"CREATE USER {schema} IDENTIFIED BY olrt QUOTA UNLIMITED ON users DEFAULT TABLESPACE users;"]
    for w in widths:
        cols = ",\n  ".join(f"{col_name(i)} {col_type(i)}{' PRIMARY KEY' if i == 1 else ''}"
                            for i in range(1, w + 1))
        pf = f" PCTFREE {pctfree[w]}" if pctfree and w in pctfree else ""
        out.append(f"CREATE TABLE {schema}.W{w} (\n  {cols}\n){pf};")
        out.append(f"ALTER TABLE {schema}.W{w} ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;")
    return out


def insert(schema, w, key, vals):
    """vals: dict col index -> literal; other columns stay NULL (not named)."""
    cols = [1] + sorted(vals)
    names = ", ".join(col_name(i) for i in cols)
    v = ", ".join(str(key) if i == 1 else vals[i] for i in cols)
    return f"INSERT INTO {schema}.W{w} ({names}) VALUES ({v});"


def update(schema, w, where, sets):
    s = ", ".join(f"{col_name(i)} = {v}" for i, v in sorted(sets.items()))
    return f"UPDATE {schema}.W{w} SET {s} WHERE {where};"


def full_row(rng, w, upto=None, start=2):
    upto = w if upto is None else upto
    return {i: lit(rng, kind(i)) for i in range(start, upto + 1)}


# ------------------------------------------------------------------ boundary scenario

BOUNDARY_WIDTHS = [63, 64, 65, 127, 128, 129, 255, 256, 257]


def boundary(name="width-boundary-mix", seed=50073):
    rng = random.Random(seed)
    schema = "OLRT_ADVB"
    setup = ddl(schema, BOUNDARY_WIDTHS, pctfree={257: 0, 129: 0})
    # every table: key 1..6 full rows, 11..16 rows that end exactly one column below each boundary
    for w in BOUNDARY_WIDTHS:
        for k in range(1, 7):
            setup.append(insert(schema, w, k, full_row(rng, w)))
        for j, b in enumerate([b for b in (63, 64, 65, 127, 128, 129, 255, 256) if b < w]):
            setup.append(insert(schema, w, 10 + j + 1, full_row(rng, w, upto=b)))
    setup.append("COMMIT;")

    wl = []
    t = lambda w: f"{schema}.W{w}"  # noqa: E731
    # 1) 50073 shape per boundary: DELETE of a trailing-NULL row on a wider table, then UPDATE of a
    #    narrower table at exactly the column past the deleted row's last value, one txn each
    for wide, narrow, lastnn in BOUNDARY_PAIRS:
        key = 10 + [b for b in (63, 64, 65, 127, 128, 129, 255, 256) if b < wide].index(lastnn) + 1
        wl.append(f"-- {wide}: delete row with last non-NULL column {lastnn}, then {narrow}-col update")
        wl.append(f"DELETE FROM {t(wide)} WHERE id = {key};")
        wl.append(update(schema, narrow, "id = 1", {narrow: lit(rng, kind(narrow))}))
        wl.append(update(schema, narrow, "id = 2", {min(narrow, lastnn + 1): lit(rng, kind(min(narrow, lastnn + 1)))}))
        wl.append("COMMIT;")
    # 2) all-NULL rows (only the key) into every width, then a high-column-only update, in one txn
    for w in BOUNDARY_WIDTHS:
        wl.append(insert(schema, w, 100, {}))
    for w in reversed(BOUNDARY_WIDTHS):
        wl.append(update(schema, w, "id = 100", {w: lit(rng, kind(w))}))
    wl.append("COMMIT;")
    # 3) trailing NULLs: set the last columns to NULL so the row shrinks, then the first column only
    for w in BOUNDARY_WIDTHS:
        wl.append(update(schema, w, "id = 3", {i: "NULL" for i in range(max(2, w - 3), w + 1)}))
        wl.append(update(schema, w, "id = 3", {2: lit(rng, "v")}))
    wl.append("COMMIT;")
    # 4) PK-only deletes alternating widths, inserts with NULL holes at the word boundaries
    for w in [257, 63, 129, 64, 256, 65, 128, 255, 127]:
        wl.append(f"DELETE FROM {t(w)} WHERE id = 4;")
        holes = {i: lit(rng, kind(i)) for i in range(2, w + 1) if i % 64 not in (0, 1, 63)}
        wl.append(insert(schema, w, 200, holes))
    wl.append("COMMIT;")
    # 5) rows that grow inside the transaction until they migrate (PCTFREE 0 on 129/257) or chain
    for w in (129, 257, 65):
        wl.append(update(schema, w, "id = 5", {i: long_val(i) for i in range(2, w + 1)}))
        wl.append(update(schema, w, "id = 5", {w: lit(rng, kind(w))}))
        wl.append(update(schema, w, "id = 6", {i: long_val(i) for i in range(2, w + 1, 2)}))
        wl.append(f"DELETE FROM {t(w)} WHERE id = 6;")
    wl.append(update(schema, 64, "id = 5", {64: lit(rng, kind(64)), 2: "NULL"}))
    wl.append("COMMIT;")
    # 6) one txn mixing everything across all widths, high column then low column, then delete
    for w in BOUNDARY_WIDTHS:
        wl.append(update(schema, w, "id = 1", {w: "NULL"}))
    for w in BOUNDARY_WIDTHS:
        wl.append(f"DELETE FROM {t(w)} WHERE id = 1;")
        wl.append(insert(schema, w, 1, full_row(rng, w, upto=min(w, 64))))
    wl.append("COMMIT;")
    readme = ("Builder per-DML state leaks (ERROR 50073 class, `Builder::releaseValues` clears `valuesSet` only up "
              "to `valuesMax`): tables of 63/64/65, 127/128/129, 255/256/257 columns; a DELETE of a row whose "
              "last non-NULL column sits just below a 64-column word, then an UPDATE on another width touching the "
              "next word; all-NULL rows, trailing NULLs, high-column-only updates, PK-only deletes, rows grown "
              "until they migrate/chain mid-transaction. Generated by `fixtures/adv/gen.py`.")
    write(name, "Builder state leaks across 63..257-column tables (50073 class, word boundaries)",
          ["adv", "wide", "builder"], [f"{schema}.W{w}" for w in BOUNDARY_WIDTHS], setup, wl, readme)


BOUNDARY_PAIRS = [(257, 129, 64), (257, 65, 63), (256, 128, 127), (129, 65, 64), (255, 64, 63), (128, 65, 64),
                  (257, 256, 255), (65, 64, 63)]


def boundary_pair(wide, narrow, lastnn):
    """One 50073-shape transaction per (wide, narrow, last non-NULL column) pair, two tables only."""
    rng = random.Random(wide * 1000 + narrow * 10 + lastnn)
    schema = f"OLRT_ADVP{wide}_{narrow}"
    setup = ddl(schema, [wide, narrow])
    setup.append(insert(schema, wide, 1, full_row(rng, wide, upto=lastnn)))
    setup.append(insert(schema, wide, 2, full_row(rng, wide)))
    for k in (1, 2):
        setup.append(insert(schema, narrow, k, full_row(rng, narrow)))
    setup.append("COMMIT;")
    hi = min(narrow, lastnn + 1)
    wl = [f"DELETE FROM {schema}.W{wide} WHERE id = 1;",
          update(schema, narrow, "id = 1", {narrow: lit(rng, kind(narrow))}),
          update(schema, narrow, "id = 2", {hi: lit(rng, kind(hi))}),
          "COMMIT;"]
    readme = (f"Smallest 50073 shape for one width pair: DELETE of a {wide}-column row whose last non-NULL column is "
              f"{lastnn}, then in the same transaction UPDATEs of a {narrow}-column table at column {narrow} and "
              f"{hi}. Part of the per-pair matrix behind width-boundary-mix (`fixtures/adv/gen.py`).")
    write(f"boundary-{wide}-{narrow}-{lastnn}", f"50073 shape: DELETE W{wide} (last non-NULL c{lastnn}) then UPDATE W{narrow}",
          ["adv", "wide", "builder", "boundary"], [f"{schema}.W{wide}", f"{schema}.W{narrow}"], setup, wl, readme)


# ------------------------------------------------------------------ random scenario

RANDOM_WIDTHS = [5, 64, 129, 257]


class Gen:
    def __init__(self, seed, size, sessions, widths, schema):
        self.rng = random.Random(seed)
        self.seed, self.size, self.n, self.widths, self.schema = seed, size, sessions, widths, schema
        self.keys = {s: {w: set() for w in widths} for s in range(sessions)}   # session view
        self.snap = {}
        self.sps = {s: [] for s in range(sessions)}
        self.open = {s: False for s in range(sessions)}
        self.next_key = {s: 1000 + s for s in range(sessions)}
        self.out = []
        self.cur = None

    def copy(self, s):
        return {w: set(v) for w, v in self.keys[s].items()}

    def emit(self, s, sql):
        if self.cur != s:
            self.out.append(f"-- @session {s + 1}")
            self.cur = s
        if not self.open[s]:
            self.open[s] = True
            self.snap[s] = self.copy(s)
            self.sps[s] = []
        self.out.append(sql)

    def new_key(self, s):
        k = self.next_key[s]
        self.next_key[s] += self.n
        return k

    def row_vals(self, w):
        r = self.rng
        shape = r.random()
        if shape < 0.15:
            return {}                                         # all NULL
        if shape < 0.45:                                      # trailing NULLs, often right at a word edge
            upto = r.choice([b for b in (2, 63, 64, 65, 127, 128, 129, 255, 256) if b <= w] + [r.randint(2, w)])
            return full_row(r, w, upto=upto)
        if shape < 0.6:                                       # sparse
            return {i: lit(r, kind(i)) for i in range(2, w + 1) if r.random() < 0.2}
        return full_row(r, w)

    def update_sets(self, w):
        r = self.rng
        m = r.random()
        if m < 0.25:
            c = w                                             # highest column only
            return {c: r.choice([lit(r, kind(c)), "NULL"])}
        if m < 0.4:
            cs = [b for b in (63, 64, 65, 127, 128, 129, 255, 256, 257) if b <= w] or [w]
            c = r.choice(cs)
            return {c: r.choice([lit(r, kind(c)), "NULL"])}
        if m < 0.5:
            return {i: long_val(i) for i in range(2, w + 1) if r.random() < 0.7}   # growth
        if m < 0.6:
            c = r.randint(2, w)
            return {c: col_name(c)}                           # no-op SET c = c
        return {i: r.choice([lit(r, kind(i)), "NULL"]) for i in r.sample(range(2, w + 1), min(w - 1, r.randint(1, 6)))}

    def step(self):
        r, schema = self.rng, self.schema
        s = r.randrange(self.n)
        w = r.choice(self.widths)
        ks = self.keys[s][w]
        x = r.random()
        if self.open[s] and x < 0.08:
            self.emit(s, "COMMIT;")
            self.open[s] = False
            return
        if self.open[s] and x < 0.11:
            self.emit(s, "ROLLBACK;")
            self.keys[s] = self.snap[s]
            self.open[s] = False
            return
        if self.open[s] and x < 0.15:
            name = f"sp{len(self.sps[s])}"
            self.emit(s, f"SAVEPOINT {name};")
            self.sps[s].append((name, self.copy(s)))
            return
        if self.open[s] and self.sps[s] and x < 0.19:
            i = r.randrange(len(self.sps[s]))
            name, keys = self.sps[s][i]
            self.emit(s, f"ROLLBACK TO SAVEPOINT {name};")
            self.sps[s] = self.sps[s][:i + 1]
            self.keys[s] = {ww: set(v) for ww, v in keys.items()}
            return
        if x < 0.21:
            self.out.append("-- @switch_logfile")
            return
        if x < 0.24:                                          # multi-row insert (INSERT ... SELECT)
            n = r.choice([3, 20, 200])
            base = self.next_key[s]
            new = [base + j * self.n for j in range(n)]
            self.next_key[s] = base + n * self.n
            vals = self.row_vals(w)
            cols = [1] + sorted(vals)
            sel = ", ".join(f"{base} + (LEVEL - 1) * {self.n}" if i == 1 else vals[i] for i in cols)
            self.emit(s, f"INSERT INTO {schema}.W{w} ({', '.join(col_name(i) for i in cols)}) "
                         f"SELECT {sel} FROM dual CONNECT BY LEVEL <= {n};")
            ks.update(new)
            return
        if ks and x < 0.27:                                   # multi-row update over own keys
            lo = min(ks)
            hi = r.choice(sorted(ks))
            self.emit(s, update(schema, w, f"MOD(id, {self.n}) = {(1000 + s) % self.n} AND id BETWEEN {lo} AND {hi}",
                                self.update_sets(w)))
            return
        if ks and x < 0.29:                                   # multi-row delete over own keys
            lo = r.choice(sorted(ks))
            hi = lo + r.randint(0, 6) * self.n
            self.emit(s, f"DELETE FROM {schema}.W{w} WHERE MOD(id, {self.n}) = {(1000 + s) % self.n} "
                         f"AND id BETWEEN {lo} AND {hi};")
            ks.difference_update({k for k in ks if lo <= k <= hi})
            return
        if not ks or x < 0.5:
            k = self.new_key(s)
            self.emit(s, insert(schema, w, k, self.row_vals(w)))
            ks.add(k)
            return
        k = r.choice(sorted(ks))
        if x < 0.62:
            self.emit(s, f"DELETE FROM {schema}.W{w} WHERE id = {k};")
            ks.discard(k)
            return
        self.emit(s, update(schema, w, f"id = {k}", self.update_sets(w)))

    def run(self):
        for _ in range(self.size):
            self.step()
        for s in range(self.n):
            if self.open[s]:
                self.emit(s, "COMMIT;")
                self.open[s] = False
        return self.out


def random_scenario(seed, size=150, sessions=3, widths=RANDOM_WIDTHS):
    name = f"random-s{seed}-n{size}"
    schema = f"OLRT_ADVR{seed}"
    rng = random.Random(seed * 7919)
    setup = ddl(schema, widths, pctfree={257: 0, 64: 0})
    g = Gen(seed, size, sessions, widths, schema)
    for w in widths:                          # a few committed rows per session to start from
        for s in range(sessions):
            for _ in range(3):
                k = g.new_key(s)
                setup.append(insert(schema, w, k, full_row(rng, w) if rng.random() < 0.6 else g.row_vals(w)))
                g.keys[s][w].add(k)
    setup.append("COMMIT;")
    wl = [f"-- seed {seed}, {size} steps, {sessions} sessions, widths {widths}; regenerate: fixtures/adv/gen.py"]
    wl += g.run()
    readme = (f"Seeded random DML mix (seed {seed}, {size} steps, {sessions} sessions) over tables of "
              f"{', '.join(map(str, widths))} columns: interleaved transactions committing out of begin order, "
              "savepoints and rollbacks, multi-row INSERT SELECT/UPDATE/DELETE, all-NULL and trailing-NULL rows, "
              "high-column-only and no-op updates, rows grown until they migrate/chain, log switches "
              "mid-transaction, NUMBER/DATE edge values. Checks the replay invariant without hand-written "
              "expectations. Same seed gives the same SQL (`fixtures/adv/gen.py`).")
    write(name, f"Seeded random DML mix, seed {seed}, {size} steps, widths {'/'.join(map(str, widths))}",
          ["adv", "random", "wide"], [f"{schema}.W{w}" for w in widths], setup, wl, readme)


# ------------------------------------------------------------------ statement-level rollback

def stmt_rollback(name="stmt-rollback-wide"):
    """Statement-level rollback inside a multi-row statement on multi-piece rows (WARN 70003 path)."""
    rng = random.Random(3)
    schema = "OLRT_ADVS"
    setup = ddl(schema, [313, 257, 5])
    for w in (313, 257, 5):
        for k in range(1, 21):
            setup.append(insert(schema, w, k, full_row(rng, w) if k % 3 else full_row(rng, w, upto=200 if w > 200 else 3)))
    setup.append(f"ALTER TABLE {schema}.W313 ADD CONSTRAINT w313_ck CHECK (c2 IS NULL OR c2 <> 'BAD');")
    setup.append(f"CREATE TABLE {schema}.CHILD (id NUMBER(10) PRIMARY KEY, pid NUMBER(10) REFERENCES {schema}.W257 (id));")
    setup.append(f"ALTER TABLE {schema}.CHILD ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;")
    setup.append(f"INSERT INTO {schema}.CHILD VALUES (1, 15);")
    setup.append("COMMIT;")
    wide313 = full_row(rng, 313)
    cols = [1] + sorted(wide313)
    sel = ", ".join("id_src" if i == 1 else wide313[i] for i in cols)
    names = ", ".join(col_name(i) for i in cols)
    wl = [
        f"UPDATE {schema}.W5 SET c2 = 'before-stmt-rollbacks' WHERE id = 1;",
        "-- multi-row INSERT of 313-column rows, the 4th collides with an existing key (id 3)",
        "-- @expect_error ORA-00001",
        f"INSERT INTO {schema}.W313 ({names}) SELECT {sel} FROM (SELECT CASE WHEN LEVEL = 4 THEN 3 ELSE 1000 + LEVEL END id_src FROM dual CONNECT BY LEVEL <= 6 ORDER BY LEVEL);",
        "-- multi-row UPDATE of high and low columns, row 5 violates the check constraint",
        "-- @expect_error ORA-02290",
        f"UPDATE {schema}.W313 SET c313 = id + 0.5, c300 = DATE '2030-06-30' - id, c298 = 'stmt-rb', c2 = CASE WHEN id = 5 THEN 'BAD' ELSE 'ok' END WHERE id BETWEEN 1 AND 8;",
        "-- multi-row DELETE of 257-column rows, row 15 has a child",
        "-- @expect_error ORA-02292",
        f"DELETE FROM {schema}.W257 WHERE id BETWEEN 10 AND 18;",
        f"UPDATE {schema}.W313 SET c313 = 2031 WHERE id = 2;",
        f"DELETE FROM {schema}.W257 WHERE id = 10;",
        "SAVEPOINT a;",
        "-- statement rollback after a savepoint, then roll back to the savepoint too",
        "-- @expect_error ORA-02290",
        f"UPDATE {schema}.W313 SET c2 = CASE WHEN id = 9 THEN 'BAD' ELSE 'x' END, c257 = -1 WHERE id BETWEEN 6 AND 12;",
        f"UPDATE {schema}.W313 SET c256 = DATE '2040-01-01', c258 = 'kept-then-rolled-back' WHERE id = 6;",
        "ROLLBACK TO SAVEPOINT a;",
        f"UPDATE {schema}.W313 SET c258 = 'after-sp' WHERE id = 7;",
        "COMMIT;",
        "-- a large wide transaction rolled back across a log switch, a short one commits meanwhile",
        f"UPDATE {schema}.W313 SET c310 = 'rb', c3 = 1 WHERE id BETWEEN 1 AND 20;",
        f"INSERT INTO {schema}.W257 ({', '.join(col_name(i) for i in range(1, 258))}) SELECT 5000 + LEVEL, "
        + ", ".join(lit(rng, kind(i)) for i in range(2, 258)) + " FROM dual CONNECT BY LEVEL <= 1500;",
        "-- @switch_logfile",
        "-- @session 2",
        f"UPDATE {schema}.W5 SET c2 = 'committed-while-other-open' WHERE id = 2;",
        "COMMIT;",
        "-- @session 1",
        f"DELETE FROM {schema}.W313 WHERE id BETWEEN 11 AND 20;",
        "ROLLBACK;",
        f"UPDATE {schema}.W313 SET c312 = DATE '2032-02-29' WHERE id = 11;",
        "COMMIT;",
    ]
    readme = ("Statement-level rollback on multi-piece rows (docs/scenario-backlog.md, x-stmt-rollback): multi-row INSERT SELECT of 313-column rows "
              "failing on the 4th row (ORA-00001), multi-row UPDATE of high and low columns failing on a CHECK "
              "constraint (ORA-02290), multi-row DELETE of 257-column rows failing on an FK child (ORA-02292), a "
              "statement rollback after a savepoint followed by ROLLBACK TO SAVEPOINT, and a 1 500-row wide "
              "transaction rolled back across a log switch while another commits. Every undone change must vanish "
              "(phantoms show in replay; WARN 70003 fails run).")
    write(name, "Statement-level rollbacks inside multi-row DML on 257/313-column rows",
          ["adv", "rollback", "wide"], [f"{schema}.W313", f"{schema}.W257", f"{schema}.W5", f"{schema}.CHILD"],
          setup, wl, readme)


# ------------------------------------------------------------------ NUMBER/DATE edges

def edges(name="number-date-edges"):
    """Edge values of VARCHAR2/NUMBER/DATE, at low (2..5) and high (254..257) column positions."""
    schema = "OLRT_ADVE"
    setup = ddl(schema, [257])
    setup.append(f"CREATE TABLE {schema}.T (id NUMBER(10) PRIMARY KEY, n153 NUMBER(15,3), n NUMBER, n1 NUMBER(1), "
                 "n10 NUMBER(10), n52 NUMBER(5,2), d DATE, v VARCHAR2(4000));")
    setup.append(f"ALTER TABLE {schema}.T ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;")
    setup.append(f"INSERT INTO {schema}.T (id) VALUES (0);")
    setup.append(f"INSERT INTO {schema}.W257 (id, c254, c255, c256, c257) VALUES (0, NULL, 1, DATE '2000-01-01', 1);")
    setup.append("COMMIT;")
    n153 = ["0", "0.000", "-0", "-0.000", "0.001", "-0.001", "999999999999.999", "-999999999999.999", "0.0005",
            "-0.0005", "1.0", "10", "1000000", "123.450", "-1", "0.1", "0.01"]
    n = ["0", "-0", "1e125", "-1e125", "9.999999999999999999999999999999999999e125", "-9.999999999999999999999999999999999999e125",
         "99999999999999999999999999999999999999", "-99999999999999999999999999999999999999",
         "0.00000000000000000000000000000000000001", "1e-127", "-1e-127", "1e-128", "100", "-100",
         "123456789012345678901234567890.12345678", "0.5", "-0.5", "1e38", "1e39", "2147483648", "-2147483649",
         "9223372036854775807", "9223372036854775808", "-9223372036854775809", "18446744073709551616", "1/3", "-2/3"]
    d = ["0001-01-01 00:00:00", "0001-12-31 23:59:59", "9999-12-31 23:59:59", "9999-01-01 00:00:00",
         "1970-01-01 00:00:00", "1969-12-31 23:59:59", "1970-01-01 00:00:01", "2000-02-29 23:59:59",
         "1600-02-29 12:00:00", "1582-10-04 23:59:59", "1582-10-15 00:00:00", "1899-12-30 00:00:00",
         "2026-03-29 02:30:00", "2026-10-25 02:30:00", "2038-01-19 03:14:08", "2262-04-11 23:47:17",
         "1677-09-21 00:12:43", "4712-12-31 23:59:59", "0099-06-15 12:30:45"]
    D = lambda s: f"TO_DATE('{s}','YYYY-MM-DD HH24:MI:SS')"  # noqa: E731
    wl = []
    k = 1
    for i in range(max(len(n153), len(n), len(d))):
        a, b, c = n153[i % len(n153)], n[i % len(n)], d[i % len(d)]
        n1 = ["0", "1", "-9", "9", "NULL"][i % 5]
        n10 = ["0", "9999999999", "-9999999999", "1", "NULL"][i % 5]
        n52 = ["0", "999.99", "-999.99", "0.01", "-0.01", "0.005"][i % 6]
        wl.append(f"INSERT INTO {schema}.T VALUES ({k}, {a}, {b}, {n1}, {n10}, {n52}, {D(c)}, NULL);")
        wl.append(f"INSERT INTO {schema}.W257 (id, c2, c3, c4, c5, c254, c255, c256, c257) VALUES "
                  f"({k}, NULL, {a}, {D(c)}, {b}, '{a}', {a}, {D(c)}, {b});")
        k += 1
    wl.append("COMMIT;")
    # updates: value -> edge, edge -> NULL, NULL -> edge, edge -> same edge, edge -> other sign
    for i in range(1, k):
        a, b, c = n153[-i % len(n153)], n[-i % len(n)], d[-i % len(d)]
        wl.append(f"UPDATE {schema}.T SET n153 = {a}, n = {b}, d = {D(c)} WHERE id = {i};")
        wl.append(f"UPDATE {schema}.W257 SET c257 = {b}, c256 = {D(c)} WHERE id = {i};")
    wl.append("COMMIT;")
    for i in range(1, k, 3):
        wl.append(f"UPDATE {schema}.T SET n153 = NULL, n = -n, d = NULL WHERE id = {i};")
        wl.append(f"UPDATE {schema}.W257 SET c257 = NULL, c256 = c256 + 1/86400, c3 = -c3, c255 = -c255 WHERE id = {i};")
        wl.append(f"UPDATE {schema}.T SET n153 = n153 WHERE id = {i + 1};")
    wl.append(f"UPDATE {schema}.T SET n153 = 0.000, n = -0, d = {D('0001-01-01 00:00:00')} WHERE id = 0;")
    wl.append(f"UPDATE {schema}.W257 SET c255 = -0.000, c257 = 1e-130 * 0, c256 = {D('9999-12-31 23:59:59')} WHERE id = 0;")
    wl.append(f"DELETE FROM {schema}.T WHERE id IN (2, 3);")
    wl.append(f"DELETE FROM {schema}.W257 WHERE id IN (2, 3);")
    wl.append("COMMIT;")
    readme = ("NUMBER and DATE edge values that matter for VARCHAR2/NUMBER/DATE tables: 0, -0, 0.000 with scale, "
              "+-999999999999.999 in NUMBER(15,3), 38-digit integers, +-9.99e125, 1e-127/1e-128 (just above the "
              "known 1e-128 cut-off), 2^31/2^63/2^64 neighbours, 1/3; DATE year 1 and 9999, 23:59:59, 1582 "
              "cutover, Feb 29, DST gap/overlap wall times, int64-nanosecond limits (1677/2262). Inserted, updated "
              "edge->edge, edge->NULL, sign flips and no-op updates, at low (2..5) and high (254..257) column "
              "positions of a 257-column table.")
    write(name, "NUMBER/DATE edge values, insert/update/delete, low and high column positions of a wide table",
          ["adv", "types"], [f"{schema}.T", f"{schema}.W257"], setup, wl, readme)


# ------------------------------------------------------------------ output

def write(name, desc, tags, tables, setup, workload, readme, extra_toml=""):
    d = OUT / name
    d.mkdir(parents=True, exist_ok=True)
    tl = ", ".join(f'"{t}"' for t in tables)
    tg = ", ".join(f'"{t}"' for t in tags)
    (d / "scenario.toml").write_text(f'description = "{desc}"\ntags = [{tg}]\ntables = [{tl}]\n'
                                     f'olr_timeout = 600\n{extra_toml}')
    (d / "setup.sql").write_text("-- generated by fixtures/adv/gen.py, do not edit\n" + "\n".join(setup) + "\n")
    (d / "workload.sql").write_text("-- generated by fixtures/adv/gen.py, do not edit\n" + "\n".join(workload) + "\n")
    (d / "README.md").write_text(readme + "\n")


SEEDS = [(1, 150), (2, 150), (3, 400)]

if __name__ == "__main__":
    # one invocation writes every scenario under scenarios/adv/ that this file owns
    boundary()
    for pair in BOUNDARY_PAIRS:
        boundary_pair(*pair)
    stmt_rollback()
    edges()
    for seed, size in SEEDS:
        random_scenario(seed, size)
