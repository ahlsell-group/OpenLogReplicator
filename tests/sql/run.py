#!/usr/bin/env python3
"""SQL regression tests for OpenLogReplicator (OLR): run real SQL on Oracle Free, let OLR read
the redo, and check that replaying OLR's output reproduces the database.

  run.py up                          start the Oracle Free container (first start takes a few minutes)
  run.py list                        list scenarios
  run.py run --image IMG [NAME...]   run scenarios (default: all) against an OLR docker image
  run.py down [--volume]             remove the container (and its data volume)

Per scenario (tests/sql/scenarios/<name>/): setup.sql, workload.sql, scenario.toml, expected.md.
Replay invariant: snapshot(before) + OLR events (in output order) == snapshot(after), where
the snapshots are `SELECT ... AS OF SCN` at the start and end of workload.sql. For updates and
deletes the before-image must also match the replayed row, which catches wrong values, wrong
transaction order, missing rows and phantom (rolled-back) rows. OLR's log must be free of
ERROR/WARN lines and OLR must stop by itself after the last archived log.
"""
import argparse
import datetime as dt
import decimal
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import tomllib

import oracledb

HERE = pathlib.Path(__file__).resolve().parent
SCENARIOS = HERE / "scenarios"
WORK = pathlib.Path(os.environ.get("OLRSQL_WORK", HERE / "work"))

# Several suites can share one Docker engine: OLRSQL_INSTANCE adds a suffix to every name.
INSTANCE = os.environ.get("OLRSQL_INSTANCE", "")
SUFFIX = f"-{INSTANCE}" if INSTANCE else ""
CONTAINER, NETWORK, VOLUME = f"olrsql-oracle{SUFFIX}", f"olrsql-net{SUFFIX}", f"olrsql-oradata{SUFFIX}"
PORT = int(os.environ.get("OLRSQL_PORT", "15221"))
ORACLE_IMAGE = os.environ.get("OLRSQL_ORACLE_IMAGE", "gvenzl/oracle-free:23.26.2-slim")
ORACLE_TZ = os.environ.get("OLRSQL_ORACLE_TZ", "Europe/Berlin")
ORACLE_GID = 54321          # oinstall in the Oracle image: redo files are 0640 oracle:oinstall
OLR_TIMEOUT_S = int(os.environ.get("OLRSQL_OLR_TIMEOUT", "300"))
OLR_BINARY = "/opt/OpenLogReplicator/OpenLogReplicator"   # path inside the OLR image
PDB, PASSWORD = "FREEPDB1", "oracle"

oracledb.defaults.fetch_decimals = True
NLS = ["ALTER SESSION SET NLS_NUMERIC_CHARACTERS = '.,'"]


# ------------------------------------------------------------------ docker / database

def docker(*args, check=True):
    return subprocess.run(["docker", *args], check=check, text=True, capture_output=True)


def exists(kind, name):
    return docker(kind, "inspect", name, check=False).returncode == 0


def connect(service):
    c = oracledb.connect(user="sys", password=PASSWORD, dsn=f"127.0.0.1:{PORT}/{service}",
                         mode=oracledb.AUTH_MODE_SYSDBA)
    for s in NLS:
        c.cursor().execute(s)
    return c


def root():
    return connect("FREE")      # CDB$ROOT: log switches, archived log list


def pdb():
    return connect(PDB)         # scenario SQL and snapshots


def up():
    if not exists("network", NETWORK):
        docker("network", "create", NETWORK)
    if not exists("volume", VOLUME):
        docker("volume", "create", VOLUME)
    if not exists("container", CONTAINER):
        docker("run", "-d", "--name", CONTAINER, "--network", NETWORK, "-p", f"127.0.0.1:{PORT}:1521",
               "--shm-size", "1g", "-e", f"ORACLE_PASSWORD={PASSWORD}", "-e", f"TZ={ORACLE_TZ}",
               "-v", f"{VOLUME}:/opt/oracle/oradata",
               "-v", f"{HERE / 'docker' / 'init'}:/container-entrypoint-initdb.d:ro", ORACLE_IMAGE)
    else:
        docker("start", CONTAINER)
    t0 = time.time()
    while time.time() - t0 < 900:
        state = docker("inspect", "-f", "{{.State.Status}}", CONTAINER).stdout.strip()
        if state == "exited":
            raise SystemExit("Oracle container exited:\n" + docker("logs", "--tail", "40", CONTAINER).stderr)
        logs = docker("logs", CONTAINER, check=False)
        if "DATABASE IS READY TO USE" in logs.stdout + logs.stderr:
            try:
                root().cursor().execute("SELECT 1 FROM dual")
                break
            except oracledb.Error:
                pass
        time.sleep(5)
    else:
        raise SystemExit("Oracle did not become ready in 15 minutes")
    with root() as c:
        mode, = c.cursor().execute("SELECT log_mode FROM v$database").fetchone()
        if mode != "ARCHIVELOG":
            raise SystemExit("database is not in ARCHIVELOG mode; the init scripts failed (run: down --volume)")
        # OLR reads the dictionary AS OF the start SCN and snapshots use AS OF as well
        c.cursor().execute("ALTER SYSTEM SET undo_retention = 86400")
    with pdb() as c:
        c.cursor().execute("ALTER TABLESPACE undotbs1 RETENTION GUARANTEE")
    print(f"Oracle ready on 127.0.0.1:{PORT} (container {CONTAINER})")


def down(volume):
    docker("rm", "-f", CONTAINER, check=False)
    if volume:
        docker("volume", "rm", VOLUME, check=False)


# ------------------------------------------------------------------ SQL scripts

SQLPLUS = re.compile(r"^\s*(SET|PROMPT|WHENEVER|SPOOL|EXIT|COLUMN|SHOW|REM)\b", re.I)
PLSQL_START = re.compile(r"^\s*(BEGIN|DECLARE|CREATE\s+(OR\s+REPLACE\s+)?(PROCEDURE|FUNCTION|PACKAGE|TRIGGER|TYPE))\b", re.I)
DIRECTIVE = re.compile(r"^\s*--\s*@(\w+)\s*(.*?)\s*$")


def parse_script(text):
    """Statements end with ';' at end of line; PL/SQL blocks with a line holding only '/'.
    Directives (comment lines): '-- @session N' switches connection (default 1),
    '-- @switch_logfile' archives the current redo log."""
    steps, buf, plsql, start = [], [], False, 0
    for no, raw in enumerate(text.splitlines(), start=1):
        line = raw.rstrip()
        if not buf:
            m = DIRECTIVE.match(line)
            if m:
                steps.append((m.group(1).lower(), m.group(2), no))
                continue
            if not line.strip() or line.strip().startswith("--") or SQLPLUS.match(line):
                continue
            plsql, start = bool(PLSQL_START.match(line)), no
        if plsql:
            if line.strip() == "/":
                steps.append(("sql", "\n".join(buf).strip(), start))
                buf = []
            else:
                buf.append(line)
        else:
            buf.append(line)
            if line.endswith(";"):
                steps.append(("sql", "\n".join(buf).strip()[:-1].strip(), start))
                buf = []
    if "\n".join(buf).strip():
        raise ValueError(f"unterminated statement starting at line {start}")
    return steps


def run_script(text, sessions, root_conn, label):
    """Run a script. sessions: {id: connection}, opened on first use."""
    current = 1
    for kind, arg, line in parse_script(text):
        if kind == "session":
            current = int(arg)
        elif kind == "switch_logfile":
            root_conn.cursor().execute("ALTER SYSTEM ARCHIVE LOG CURRENT")   # returns once archived
        elif kind == "sql":
            if sessions.get(current) is None:
                sessions[current] = pdb()
            conn = sessions[current]
            try:
                word = arg.upper()
                if word == "COMMIT":
                    conn.commit()
                elif word == "ROLLBACK":
                    conn.rollback()
                else:
                    conn.cursor().execute(arg)
            except oracledb.DatabaseError as e:
                raise RuntimeError(f"{label}:{line}: {str(e).splitlines()[0]}\n{arg[:300]}") from None
        else:
            raise ValueError(f"{label}:{line}: unknown directive @{kind}")


# ------------------------------------------------------------------ values

def kind_of(data_type):
    t = data_type.upper()
    if t in ("NUMBER", "FLOAT", "INTEGER"):
        return "num"
    if t == "DATE":
        return "date"
    if t.startswith("TIMESTAMP") and "TIME ZONE" not in t:
        return "ts"
    return "str"


def select_expr(col, data_type):
    k, q = kind_of(data_type), f'"{col}"'
    if k == "date":
        return f"TO_CHAR({q}, 'YYYY-MM-DD\"T\"HH24:MI:SS')"
    if k == "ts":
        return f"TO_CHAR({q}, 'YYYY-MM-DD\"T\"HH24:MI:SS.FF9')"
    return q


def canon(v, data_type):
    """One comparable string (or None) per value. Numbers are exact decimals, never floats."""
    if v is None:
        return None
    k = kind_of(data_type)
    if k == "num":
        if isinstance(v, float):
            return f"float:{v!r}"          # JSON is parsed with Decimal, a float means lost precision
        d = decimal.Decimal(str(v).strip())
        if d.is_zero():
            return "0"
        s = format(d.normalize(), "f")
        return s.rstrip("0").rstrip(".") if "." in s else s
    if k in ("date", "ts"):
        if isinstance(v, (int, decimal.Decimal)) and not isinstance(v, bool):   # epoch nanoseconds
            secs, nanos = divmod(int(v), 10**9)
            base = (dt.datetime(1970, 1, 1) + dt.timedelta(seconds=secs)).strftime("%Y-%m-%dT%H:%M:%S")
            return f"{base}.{nanos:09d}" if k == "ts" else (base if nanos == 0 else f"{base}.{nanos:09d}")
        return str(v)
    return v if isinstance(v, str) else str(v)


# ------------------------------------------------------------------ record (phase 1)

class Scenario:
    def __init__(self, path):
        self.path, self.name = path, path.name
        meta = tomllib.loads((path / "scenario.toml").read_text())
        self.description = meta["description"]
        self.tables = [t.upper() for t in meta["tables"]]
        self.known_failing = meta.get("known_failing")   # reason string: failure is reported but not counted
        self.events = meta.get("events")      # optional exact event counts, {"c": n, "u": n, "d": n}

    def sql(self, name):
        p = self.path / name
        return p.read_text() if p.exists() else ""


def discover(names):
    found = [Scenario(p.parent) for p in sorted(SCENARIOS.glob("*/scenario.toml"))]
    if names:
        missing = set(names) - {s.name for s in found}
        if missing:
            raise SystemExit(f"unknown scenario(s): {', '.join(sorted(missing))}")
        found = [s for s in found if s.name in names]
    return found


def table_info(conn, owner_table):
    owner, table = owner_table.split(".")
    cur = conn.cursor()
    cols = cur.execute("""SELECT column_name, data_type FROM dba_tab_cols
                           WHERE owner = :o AND table_name = :t AND hidden_column = 'NO'
                           ORDER BY column_id""", o=owner, t=table).fetchall()
    key = [r[0] for r in cur.execute("""SELECT cc.column_name FROM dba_constraints c
                 JOIN dba_cons_columns cc ON cc.owner = c.owner AND cc.constraint_name = c.constraint_name
                WHERE c.owner = :o AND c.table_name = :t AND c.constraint_type = 'P'
                ORDER BY cc.position""", o=owner, t=table).fetchall()]
    if not cols or not key:
        raise RuntimeError(f"{owner_table}: table missing or without primary key (scenarios need one)")
    return {"columns": [{"name": n, "type": t} for n, t in cols], "key": key}


def snapshot(conn, owner_table, info, scn):
    owner, table = owner_table.split(".")
    exprs = ", ".join(select_expr(c["name"], c["type"]) for c in info["columns"])
    rows = conn.cursor().execute(f'SELECT {exprs} FROM "{owner}"."{table}" AS OF SCN {scn}').fetchall()
    return [{c["name"]: canon(v, c["type"]) for c, v in zip(info["columns"], r)} for r in rows]


def current_scn(conn):
    return int(conn.cursor().execute("SELECT current_scn FROM v$database").fetchone()[0])


def current_seq(conn):
    return int(conn.cursor().execute(
        "SELECT sequence# FROM v$log WHERE status = 'CURRENT' AND thread# = 1").fetchone()[0])


def record(sc):
    """Run setup and workload; returns the SCN/sequence range and the before/after snapshots."""
    rconn, sessions = root(), {1: pdb()}
    try:
        run_script(sc.sql("setup.sql"), sessions, rconn, "setup.sql")
        sessions[1].commit()
        # AS OF SCN fails with ORA-01466 when the table was created or altered within a few
        # seconds of that SCN (SCN-to-time mapping granularity)
        time.sleep(5)
        rconn.cursor().execute("ALTER SYSTEM ARCHIVE LOG CURRENT")
        start_seq, start_scn = current_seq(rconn), current_scn(rconn)
        info = {t: table_info(sessions[1], t) for t in sc.tables}
        before = {t: snapshot(sessions[1], t, info[t], start_scn) for t in sc.tables}
        run_script(sc.sql("workload.sql"), sessions, rconn, "workload.sql")
        for c in sessions.values():
            c.commit()                    # a workload must end committed; no-op when it did
        end_scn = current_scn(rconn)
        rconn.cursor().execute("ALTER SYSTEM ARCHIVE LOG CURRENT")
        end_seq = current_seq(rconn) - 1
        after = {t: snapshot(sessions[1], t, info[t], end_scn) for t in sc.tables}
        return {"start_scn": start_scn, "end_scn": end_scn, "start_seq": start_seq, "end_seq": end_seq,
                "info": info, "before": before, "after": after}
    finally:
        for c in sessions.values():
            try:
                c.rollback()
                c.close()
            except oracledb.Error:
                pass
        rconn.close()


# ------------------------------------------------------------------ OLR (phase 2)

def olr_config(rec, sc):
    return {
        "version": "2.0.0", "log-level": 3, "trace": 0,
        "memory": {"min-mb": 32, "max-mb": 1024},
        "state": {"type": "disk", "path": "work/state", "interval-s": 600},
        "source": [{
            "alias": "S1", "name": PDB,
            "reader": {"type": "online", "user": "olr", "password": "olr",
                       "server": f"//{CONTAINER}:1521/{PDB}",
                       "log-archive-format": "o1_mf_%t_%s_%h_.arc", "start-scn": rec["start_scn"]},
            "format": {"type": "json", "column": 2, "schema": 1, "xid": 0, "flush-buffer": 0},
            "arch": "online", "flags": 0,
            # stop by itself once all archived logs of the recording are read
            "debug": {"stop-log-switches": rec["end_seq"] - rec["start_seq"] + 1},
            "filter": {"table": [{"owner": t.split(".")[0], "table": t.split(".")[1]} for t in sc.tables]},
        }],
        "target": [{"alias": "FILE", "source": "S1",
                    "writer": {"type": "file", "output": "work/output.jsonl", "new-line": 1,
                               "write-buffer-flush-size": 0}}],
    }


def run_olr(image, rec, sc, workdir):
    if workdir.exists():
        shutil.rmtree(workdir)
    (workdir / "state").mkdir(parents=True)
    os.chmod(workdir, 0o777)
    os.chmod(workdir / "state", 0o777)
    (workdir / "config.json").write_text(json.dumps(olr_config(rec, sc), indent=2))
    name = "olrsql-olr-" + hashlib.sha1(str(workdir).encode()).hexdigest()[:10]
    docker("rm", "-f", name, check=False)
    cmd = ["docker", "run", "--name", name, "--network", NETWORK, "--user", f"{os.getuid()}:{ORACLE_GID}",
           "-v", f"{VOLUME}:/opt/oracle/oradata:ro", "-v", f"{workdir.resolve()}:/opt/OpenLogReplicator/work",
           "--entrypoint", OLR_BINARY, image, "-f", "work/config.json"]
    timed_out = False
    t0 = time.time()
    with open(workdir / "olr.log", "w") as logf:
        proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT)
        try:
            rc = proc.wait(timeout=OLR_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            timed_out = True
            docker("kill", name, check=False)
            rc = proc.wait()
    docker("rm", "-f", name, check=False)
    return {"exit_code": rc, "timed_out": timed_out, "seconds": round(time.time() - t0, 1)}


# ------------------------------------------------------------------ checks (phase 3)

LOG_LINE = re.compile(r"^\S+ \S+ (ERROR|WARN|FATAL)\s+(\d+)\s*(.*)$")
BENIGN = [r"^10003 file: work/state/.*chkpt\.json - get metadata returned: No such file or directory"]


def check_run(workdir, info):
    problems = []
    log = (workdir / "olr.log").read_text(errors="replace")
    for line in log.splitlines():
        m = LOG_LINE.match(line)
        if m and not any(re.search(p, f"{m.group(2)} {m.group(3)}") for p in BENIGN):
            problems.append(f"{m.group(1)} {m.group(2)} {m.group(3)}")
    if info["timed_out"]:
        problems.append(f"timed out after {info['seconds']}s (did not stop after the last log switch)")
    elif "exhausted number of log switches" not in log:
        problems.append("OLR did not read to the end of the recorded redo")
    if info["exit_code"] not in (0, None) and not info["timed_out"] and not problems:
        problems.append(f"exit status {info['exit_code']}")
    return problems


def read_events(path):
    """Returns (events in output order, problems). An event: op, table, before, after, xid."""
    events, problems, open_xid = [], [], None
    if not path.exists():
        return events, ["no output file"]
    for ln, line in enumerate(path.read_text().splitlines(), start=1):
        if not line.strip():
            continue
        try:
            msg = json.loads(line, parse_float=decimal.Decimal)
        except json.JSONDecodeError as e:
            problems.append(f"output line {ln}: invalid JSON: {e}")
            continue
        for p in msg.get("payload", []):
            op = p.get("op")
            if op == "begin":
                if open_xid is not None:
                    problems.append(f"output line {ln}: transaction {msg.get('xid')} begins inside {open_xid}")
                open_xid = msg.get("xid")
            elif op == "commit":
                open_xid = None
            elif op in ("c", "u", "d"):
                if open_xid is None or msg.get("xid") != open_xid:
                    problems.append(f"output line {ln}: {op} for xid {msg.get('xid')} outside its transaction")
                sch = p.get("schema", {})
                events.append({"op": op, "table": f"{sch.get('owner')}.{sch.get('table')}",
                               "before": p.get("before"), "after": p.get("after"), "line": ln})
    if open_xid is not None:
        problems.append(f"transaction {open_xid} has no commit in the output")
    return events, problems


def check_replay(sc, rec, events):
    problems = []
    keys = {t: rec["info"][t]["key"] for t in sc.tables}
    types = {t: {c["name"]: c["type"] for c in rec["info"][t]["columns"]} for t in sc.tables}
    cols = {t: list(types[t]) for t in sc.tables}
    state = {t: {tuple(r[k] for k in keys[t]): dict(r) for r in rec["before"][t]} for t in sc.tables}

    def image(img, t):
        return {c: canon(v, types[t].get(c, "VARCHAR2")) for c, v in (img or {}).items()}

    for ev in events:
        t, op = ev["table"], ev["op"]
        if t not in state:
            problems.append(f"output line {ev['line']}: event for unexpected table {t}")
            continue
        before, after = image(ev["before"], t), image(ev["after"], t)
        where = f"output line {ev['line']} {op} {t}"
        if op == "c":
            row = {c: after.get(c) for c in cols[t]}
            k = tuple(row[c] for c in keys[t])
            if k in state[t]:
                problems.append(f"{where}: insert of existing key {k}")
            state[t][k] = row
            continue
        k = tuple(before.get(c) for c in keys[t])
        cur = state[t].get(k)
        if cur is None:
            problems.append(f"{where}: key {k} not present")
            continue
        bad = {c: (before[c], cur[c]) for c in before if c in cur and before[c] != cur[c]}
        if bad:
            problems.append(f"{where} key {k}: before-image differs from replayed row: {short(bad)}")
        del state[t][k]
        if op == "u":
            row = {**cur, **after}
            state[t][tuple(row[c] for c in keys[t])] = row
    for t in sc.tables:
        want = {tuple(r[k] for k in keys[t]): r for r in rec["after"][t]}
        got = state[t]
        for k in sorted(want.keys() - got.keys(), key=repr)[:5]:
            problems.append(f"{t} key {k}: in the database, missing after replay")
        for k in sorted(got.keys() - want.keys(), key=repr)[:5]:
            problems.append(f"{t} key {k}: present after replay, not in the database")
        n = 0
        for k in sorted(want.keys() & got.keys(), key=repr):
            bad = {c: (want[k][c], got[k].get(c)) for c in cols[t] if want[k][c] != got[k].get(c)}
            if bad:
                n += 1
                if n <= 5:
                    problems.append(f"{t} key {k}: database vs replay: {short(bad)}")
    return problems


def short(diffs, limit=4):
    items = [f"{c}: {a!r} != {b!r}" for c, (a, b) in sorted(diffs.items())]
    return "; ".join(items[:limit]) + (f" (+{len(items) - limit} more)" if len(items) > limit else "")


def check_counts(sc, events):
    if not sc.events:
        return []
    got = {op: sum(1 for e in events if e["op"] == op) for op in "cud"}
    want = {op: sc.events.get(op, 0) for op in "cud"}
    return [] if got == want else [f"event counts (c/u/d) {got} != expected {want}"]


# ------------------------------------------------------------------ main

def run(image, names, keep):
    if docker("image", "inspect", image, check=False).returncode != 0:
        raise SystemExit(f"docker image {image} not found locally")
    if not exists("container", CONTAINER):
        raise SystemExit("Oracle container not running; run: run.py up")
    results = []
    for sc in discover(names):
        print(f"== {sc.name}: {sc.description}", flush=True)
        workdir = WORK / re.sub(r"[^A-Za-z0-9_.-]+", "_", image) / sc.name
        try:
            rec = record(sc)
            info = run_olr(image, rec, sc, workdir)
            events, parse_problems = read_events(workdir / "output.jsonl")
            problems = check_run(workdir, info) + parse_problems + check_replay(sc, rec, events) + check_counts(sc, events)
        except Exception as e:      # setup/workload errors are test bugs, report them as failures
            problems, events = [f"harness error: {e}"], []
        status = "PASS" if not problems else "FAIL"
        if sc.known_failing:
            status = "XPASS (known failure fixed?)" if not problems else "XFAIL (" + sc.known_failing + ")"
            results.append((sc.name, []))
        else:
            results.append((sc.name, problems))
        print(f"   {status}  ({len(events)} events)", flush=True)
        for p in problems[:12]:
            print(f"     - {p}")
        if len(problems) > 12:
            print(f"     ... {len(problems) - 12} more")
        if not keep and not problems:
            shutil.rmtree(workdir, ignore_errors=True)
    failed = [n for n, p in results if p]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("up")
    sub.add_parser("list")
    d = sub.add_parser("down")
    d.add_argument("--volume", action="store_true", help="also remove the data volume")
    r = sub.add_parser("run")
    r.add_argument("--image", required=True, help="OLR docker image, e.g. bersler/openlogreplicator:2.0.0")
    r.add_argument("--keep", action="store_true", help="keep config, log and output of passing scenarios too")
    r.add_argument("names", nargs="*", help="scenario names (default: all)")
    a = ap.parse_args()
    if a.cmd == "up":
        up()
    elif a.cmd == "down":
        down(a.volume)
    elif a.cmd == "list":
        for sc in discover([]):
            print(f"{sc.name:28} {sc.description}")
    else:
        sys.exit(run(a.image, a.names, a.keep))


if __name__ == "__main__":
    main()
