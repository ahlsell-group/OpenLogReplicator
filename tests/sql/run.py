#!/usr/bin/env python3
"""SQL regression tests for OpenLogReplicator (OLR): run real SQL on Oracle Free, let OLR read
the redo, and check that replaying OLR's output reproduces the database.

  run.py up                          start the Oracle Free container (first start takes a few minutes)
  run.py list                        list scenarios
  run.py run --image IMG [NAME...]   run scenarios (default: all) against an OLR docker image
  run.py down [--volume]             remove the container (and its data volume)

A scenario runs in one of three ways. By default the workload is recorded first and OLR reads the archived
redo afterwards (file writer, start-scn, debug.stop-log-switches). With a `-- @start` line in workload.sql the
statements before it run first (e.g. a transaction left open), then the start SCN is taken and OLR is started
while the rest of the workload runs. With `mode = "network"` OLR uses its network writer and the workload drives
a client (`-- @client ...` lines, see Client), like Debezium does.

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
import socket
import struct
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


def run_script(text, sessions, root_conn, label, hooks=None, connect_session=None):
    """Run a script. sessions: {id: connection}, opened on first use with connect_session (default: the PDB).
    hooks: {directive: function(arg)} for the directives the runner handles itself (@client)."""
    current = 1
    for kind, arg, line in parse_script(text):
        if kind == "session":
            current = int(arg)
        elif kind == "switch_logfile":
            root_conn.cursor().execute("ALTER SYSTEM ARCHIVE LOG CURRENT")   # returns once archived
        elif kind == "sleep":
            time.sleep(float(arg))
        elif hooks and kind in hooks:
            try:
                hooks[kind](arg)
            except (OSError, RuntimeError) as e:
                raise RuntimeError(f"{label}:{line}: @{kind} {arg}: {e}") from None
        elif kind == "sql":
            if sessions.get(current) is None:
                sessions[current] = (connect_session or pdb)()
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
    if t in ("BINARY_FLOAT", "BINARY_DOUBLE"):
        return "f32" if t == "BINARY_FLOAT" else "f64"
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
    if k in ("f32", "f64"):   # IEEE values: equal when they read back as the same float/double
        f = float(v)
        if k == "f32":
            f = struct.unpack("<f", struct.pack("<f", f))[0]
        return repr(f)
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
        self.mode = meta.get("mode", "file")  # "file" or "network"
        self.checks = meta.get("checks", ["run", "replay", "counts"])
        # merged into the OLR configuration
        self.olr_reader, self.olr_format = meta.get("olr_reader", {}), meta.get("olr_format", {})
        self.expect_exit = meta.get("expect_exit")        # "zero" / "nonzero": OLR's exit code is checked
        self.expect_log = meta.get("expect_log", [])      # regexes of ERROR/WARN lines that must appear (and are allowed)
        self.archive_gap = meta.get("archive_gap")        # {"index": n}: hide the n-th archived log of the range from OLR
        self.stop = meta.get("stop")                      # "TERM": stop OLR with SIGTERM once the output is complete
        self.commit_time_column = meta.get("commit_time_column")   # TIMESTAMP column set to the UTC time of the DML
        self.xid_column = meta.get("xid_column")          # column holding the transaction's V$TRANSACTION.XID (hex)
        # "root": tables, workload and OLR in CDB$ROOT (no V$PDBS row, like a non-CDB); OLR logs in as olr_user
        self.container = meta.get("container", "pdb")
        self.olr_user = meta.get("olr_user", {"user": "olr", "password": "olr"})

    def connect(self):
        return root() if self.container == "root" else pdb()

    @property
    def live(self):
        """OLR runs during the workload (instead of reading the recorded redo afterwards)."""
        return self.mode == "network" or self.stop is not None or re.search(r"^\s*--\s*@start\s*$", self.sql("workload.sql"), re.M)

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


def archived_logs(conn, first_seq, last_seq):
    rows = conn.cursor().execute("""SELECT name FROM v$archived_log
                                     WHERE thread# = 1 AND sequence# BETWEEN :a AND :b AND name IS NOT NULL
                                       AND deleted = 'NO' AND standby_dest = 'NO'
                                     ORDER BY sequence#""", a=first_seq, b=last_seq).fetchall()
    return [r[0] for r in rows]


def split_start(workload):
    """The part before a `-- @start` line runs before the start SCN is taken (not captured)."""
    m = re.search(r"^\s*--\s*@start\s*$", workload, re.M)
    return (workload[:m.start()], workload[m.end():]) if m else ("", workload)


def record(sc, image=None, workdir=None):
    """Run setup and workload; returns the SCN/sequence range and the before/after snapshots. For a live scenario
    OLR is started at the start SCN and runs during the workload; the result of that run is returned too."""
    rconn, sessions = root(), {1: sc.connect()}
    olr = client = None
    try:
        run_script(sc.sql("setup.sql"), sessions, rconn, "setup.sql", connect_session=sc.connect)
        sessions[1].commit()
        # AS OF SCN fails with ORA-01466 when the table was created or altered within a few
        # seconds of that SCN (SCN-to-time mapping granularity)
        time.sleep(5)
        rconn.cursor().execute("ALTER SYSTEM ARCHIVE LOG CURRENT")
        before_start, workload = split_start(sc.sql("workload.sql"))
        first_seq = current_seq(rconn)            # the redo log the statements before @start begin in
        if before_start:
            run_script(before_start, sessions, rconn, "workload.sql", connect_session=sc.connect)
            rconn.cursor().execute("ALTER SYSTEM ARCHIVE LOG CURRENT")
        start_seq, start_scn = current_seq(rconn), current_scn(rconn)
        if not before_start:
            first_seq = start_seq
        info = {t: table_info(sessions[1], t) for t in sc.tables}
        before = {t: snapshot(sessions[1], t, info[t], start_scn) for t in sc.tables}
        hooks = {}
        if sc.live:
            # OLR reads from the start SCN while the workload runs. A build which starts with the redo log of the
            # oldest open transaction reads (start_seq - first_seq) logs more; both stop after the same number of
            # log switches, the padding below lets a build which starts at start_seq reach that number too.
            switches = len(re.findall(r"^\s*--\s*@switch_logfile\s*$", workload, re.M)) + 1 + (start_seq - first_seq)
            stop_switches = None if sc.mode == "network" or sc.stop else switches
            olr = OlrProcess(image, workdir, olr_config(start_scn, stop_switches, sc))
            olr.start(publish=sc.mode == "network")
            if sc.mode == "network":
                client = Client(olr.port, start_scn, workdir / "output.jsonl")
                hooks["client"] = client.command
            else:
                # positioned: the redo log to start with is chosen, the transactions open now are known
                olr.wait_for_log(r"starting with new batch with seq|processing redo log")
        run_script(workload, sessions, rconn, "workload.sql", hooks, connect_session=sc.connect)
        for c in sessions.values():
            c.commit()                    # a workload must end committed; no-op when it did
        end_scn = current_scn(rconn)
        rconn.cursor().execute("ALTER SYSTEM ARCHIVE LOG CURRENT")
        end_seq = current_seq(rconn) - 1
        after = {t: snapshot(sessions[1], t, info[t], end_scn) for t in sc.tables}
        rec = {"start_scn": start_scn, "end_scn": end_scn, "start_seq": start_seq, "end_seq": end_seq,
               "first_seq": first_seq, "info": info, "before": before, "after": after,
               "archived": archived_logs(rconn, start_seq, end_seq)}
        result = None
        if olr is not None:
            if client is not None:
                client.command("drain")
                client.save()
                result = olr.wait(stop="TERM")
            elif sc.stop:
                wait_quiet(workdir / "output.jsonl")
                result = olr.wait(stop=sc.stop)
            else:
                for _ in range(start_seq - first_seq):
                    rconn.cursor().execute("ALTER SYSTEM ARCHIVE LOG CURRENT")
                result = olr.wait()
        return rec, result
    finally:
        if client is not None:
            client.close()
        if olr is not None:
            olr.remove()
        for c in sessions.values():
            try:
                c.rollback()
                c.close()
            except oracledb.Error:
                pass
        rconn.close()


def wait_quiet(path, idle=5.0, timeout=OLR_TIMEOUT_S):
    """Waits until the output file has not grown for `idle` seconds."""
    t0, last, since = time.time(), -1, time.time()
    while time.time() - t0 < timeout:
        size = path.stat().st_size if path.exists() else 0
        if size != last:
            last, since = size, time.time()
        elif time.time() - since >= idle and size > 0:
            return
        time.sleep(0.5)


# ------------------------------------------------------------------ OLR (phase 2)

NET_PORT = 5000     # network writer port inside the OLR container


def olr_config(start_scn, stop_log_switches, sc):
    service = "FREE" if sc.container == "root" else PDB
    reader = {"type": "online", "user": sc.olr_user["user"], "password": sc.olr_user["password"],
              "server": f"//{CONTAINER}:1521/{service}", "log-archive-format": "o1_mf_%t_%s_%h_.arc"}
    if sc.mode != "network":
        reader["start-scn"] = start_scn       # with the network writer the client sends the start SCN
    # "{oracle_tz}" in a value is the database host's time zone (OLRSQL_ORACLE_TZ)
    reader.update({k: v.replace("{oracle_tz}", ORACLE_TZ) if isinstance(v, str) else v for k, v in sc.olr_reader.items()})
    source = {
        "alias": "S1", "name": PDB, "reader": reader,
        "format": {"type": "json", "column": 2, "schema": 1, "xid": 0, "flush-buffer": 0, **sc.olr_format},
        "arch": "online", "flags": 0,
        "filter": {"table": [{"owner": t.split(".")[0], "table": t.split(".")[1]} for t in sc.tables]},
    }
    if stop_log_switches is not None:
        # stop by itself once all archived logs of the recording are read
        source["debug"] = {"stop-log-switches": stop_log_switches}
    if sc.mode == "network":
        writer = {"type": "network", "uri": f"0.0.0.0:{NET_PORT}"}
    else:
        writer = {"type": "file", "output": "work/output.jsonl", "new-line": 1, "write-buffer-flush-size": 0}
    return {
        "version": "2.0.0", "log-level": 3, "trace": 0,
        "memory": {"min-mb": 32, "max-mb": 1024},
        "state": {"type": "disk", "path": "work/state", "interval-s": 600},
        "source": [source],
        "target": [{"alias": "OUT", "source": "S1", "writer": writer}],
    }


class OlrProcess:
    """One OLR container reading the database (redo volume mounted read-only), output in workdir."""

    def __init__(self, image, workdir, config):
        self.image, self.workdir = image, workdir
        if workdir.exists():
            shutil.rmtree(workdir)
        (workdir / "state").mkdir(parents=True)
        os.chmod(workdir, 0o777)
        os.chmod(workdir / "state", 0o777)
        (workdir / "config.json").write_text(json.dumps(config, indent=2))
        self.name = "olrsql-olr-" + hashlib.sha1(str(workdir).encode()).hexdigest()[:10]
        self.port = None
        self.proc = self.logf = None
        self.t0 = time.time()

    def start(self, publish=False):
        docker("rm", "-f", self.name, check=False)
        cmd = ["docker", "run", "--name", self.name, "--network", NETWORK, "--user", f"{os.getuid()}:{ORACLE_GID}",
               "-v", f"{VOLUME}:/opt/oracle/oradata:ro", "-v", f"{self.workdir.resolve()}:/opt/OpenLogReplicator/work"]
        if publish:
            with socket.socket() as sk:      # a free port on this host for the network writer
                sk.bind(("127.0.0.1", 0))
                self.port = sk.getsockname()[1]
            cmd += ["-p", f"127.0.0.1:{self.port}:{NET_PORT}"]
        cmd += ["--entrypoint", OLR_BINARY, self.image, "-f", "work/config.json"]
        self.logf = open(self.workdir / "olr.log", "w")
        self.proc = subprocess.Popen(cmd, stdout=self.logf, stderr=subprocess.STDOUT)
        self.t0 = time.time()

    def log(self):
        return (self.workdir / "olr.log").read_text(errors="replace")

    def wait_for_log(self, pattern, timeout=120):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if re.search(pattern, self.log()):
                return
            if self.proc.poll() is not None:
                raise RuntimeError(f"OLR ended before logging /{pattern}/ (exit {self.proc.returncode})")
            time.sleep(0.5)
        raise RuntimeError(f"OLR did not log /{pattern}/ within {timeout}s")

    def wait(self, timeout=OLR_TIMEOUT_S, stop=None):
        """Waits for OLR to end; stop="TERM" sends SIGTERM first (docker stop: SIGKILL after 30 s)."""
        timed_out = False
        if stop == "TERM":
            docker("stop", "-t", "30", self.name, check=False)
        try:
            rc = self.proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            docker("kill", self.name, check=False)
            rc = self.proc.wait()
        return {"exit_code": rc, "timed_out": timed_out, "seconds": round(time.time() - self.t0, 1)}

    def remove(self):
        if self.proc is not None and self.proc.poll() is None:
            docker("kill", self.name, check=False)
            self.proc.wait()
        docker("rm", "-f", self.name, check=False)
        if self.logf:
            self.logf.close()


def oracle_exec(*args):
    return docker("exec", "-u", "oracle", CONTAINER, *args, check=False)


def run_olr(image, rec, sc, workdir):
    """OLR reads the recorded range and stops by itself after its last archived log."""
    olr = OlrProcess(image, workdir, olr_config(rec["start_scn"], rec["end_seq"] - rec["start_seq"] + 1, sc))
    hidden = None
    try:
        if sc.archive_gap:
            # Hide one archived log of the range from OLR; it is put back afterwards
            victim = rec["archived"][sc.archive_gap["index"]]
            hidden = victim + ".hidden"
            r = oracle_exec("mv", victim, hidden)
            if r.returncode != 0:
                raise RuntimeError(f"cannot hide {victim}: {r.stderr.strip()}")
        olr.start()
        return olr.wait()
    finally:
        olr.remove()
        if hidden:
            oracle_exec("mv", hidden, victim)


# ------------------------------------------------------------------ network client

def _varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def _fields(data):
    """Decodes the varint fields of a protobuf message: {field number: value}; other wire types are skipped."""
    fields, pos = {}, 0
    while pos < len(data):
        key, pos = _read_varint(data, pos)
        num, wire = key >> 3, key & 7
        if wire == 0:
            fields[num], pos = _read_varint(data, pos)
        elif wire == 1:
            pos += 8
        elif wire == 2:
            size, pos = _read_varint(data, pos)
            pos += size
        elif wire == 5:
            pos += 4
        else:
            raise RuntimeError(f"unexpected protobuf wire type {wire}")
    return fields


def _read_varint(data, pos):
    n = shift = 0
    while True:
        b = data[pos]
        pos += 1
        n |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return n, pos


class Client:
    """A client of OLR's network writer (proto/OraProtoBuf.proto: RedoRequest/RedoResponse, each message framed by a
    little-endian uint32 length; data messages are the JSON text of the format), behaving like a Debezium connector
    that stores offsets in Kafka. Workload directives:

      -- @client connect      first time START from the start SCN, afterwards CONTINUE from the stored (c_scn, c_idx)
      -- @client read N       receive N messages
      -- @client commits N    receive messages until N commits arrived
      -- @client store        the messages received so far are "written to Kafka", their last position is the offset
      -- @client confirm      CONFIRM the stored offset (OLR may release everything up to it)
      -- @client disconnect   close the connection; received but not stored messages are lost, like on a task restart
      -- @client drain [S]    receive until nothing arrives for S seconds (default 5) and store

    Only stored messages make up the output, so a message which OLR does not send again after CONTINUE is missing
    from the replay and a message sent twice is a duplicate.
    """
    INFO, START, CONTINUE, CONFIRM = 0, 1, 2, 3
    READY, REPLICATE = 0, 4

    def __init__(self, port, start_scn, output):
        self.port, self.start_scn, self.output = port, start_scn, output
        self.sock = None
        self.pending, self.stored = [], []
        self.offset = None          # (c_scn, c_idx) of the last stored message
        self.requests = []          # for the log in the work directory

    def command(self, arg):
        words = arg.split()
        cmd, args = words[0], words[1:]
        if cmd == "connect":
            self.connect()
        elif cmd == "read":
            for _ in range(int(args[0])):
                self.receive()
        elif cmd == "commits":
            want = int(args[0])
            while want > 0:
                want -= any(p.get("op") == "commit" for p in self.receive().get("payload", []))
        elif cmd == "store":
            self.store()
        elif cmd == "confirm":
            if self.offset is not None:
                self.request(self.CONFIRM, c_scn=self.offset[0], c_idx=self.offset[1])
        elif cmd == "disconnect":
            self.close()
            self.pending = []
        elif cmd == "drain":
            idle = float(args[0]) if args else 5.0
            while self.receive(timeout=idle, missing_ok=True) is not None:
                pass
            self.store()
        else:
            raise RuntimeError(f"unknown client command: {cmd}")

    def connect(self):
        # Until OLR listens, docker's port proxy accepts the connection and closes it at once
        t0 = time.time()
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=60)
                code = self.request(self.INFO)
                break
            except (OSError, RuntimeError):
                self.close()
                if time.time() - t0 > 60:
                    raise
                time.sleep(0.5)
        if code == self.READY:
            code = self.request(self.START, scn=self.start_scn)
        elif code == self.REPLICATE and self.offset is not None:
            code = self.request(self.CONTINUE, c_scn=self.offset[0], c_idx=self.offset[1])
        elif code == self.REPLICATE:
            code = self.request(self.CONTINUE)
        if code != self.REPLICATE:
            raise RuntimeError(f"OLR answered {code} instead of REPLICATE")

    def request(self, code, scn=None, c_scn=None, c_idx=None):
        body = _varint(0x08) + _varint(code) + _varint(0x12) + _varint(len(PDB)) + PDB.encode()
        if scn is not None:
            body += _varint(0x18) + _varint(scn)
        if c_scn is not None:
            body += _varint(0x40) + _varint(c_scn) + _varint(0x48) + _varint(c_idx)
        self.sock.sendall(struct.pack("<I", len(body)) + body)
        self.requests.append({"code": code, "scn": scn, "c_scn": c_scn, "c_idx": c_idx})
        if code == self.CONFIRM:
            return None
        return _fields(self.frame()).get(1, 0)

    def frame(self):
        size, = struct.unpack("<I", self.exact(4))
        if size == 0xFFFFFFFF:
            size, = struct.unpack("<Q", self.exact(8))
        return self.exact(size)

    def exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise RuntimeError("connection closed by OLR")
            buf += chunk
        return bytes(buf)

    def receive(self, timeout=60.0, missing_ok=False):
        self.sock.settimeout(timeout)
        try:
            raw = self.frame().decode()
        except socket.timeout:
            if missing_ok:
                return None
            raise RuntimeError(f"no message from OLR within {timeout}s") from None
        msg = json.loads(raw, parse_float=decimal.Decimal)
        self.pending.append(raw)
        return msg

    def store(self):
        if self.pending:
            last = json.loads(self.pending[-1])
            self.offset = (int(last["c_scn"]), int(last["c_idx"]))
            self.stored += self.pending
            self.pending = []

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def save(self):
        self.output.write_text("".join(m + "\n" for m in self.stored))
        (self.output.parent / "client.json").write_text(json.dumps(self.requests, indent=1))


# ------------------------------------------------------------------ checks (phase 3)

LOG_LINE = re.compile(r"^\S+ \S+ (ERROR|WARN|FATAL)\s+(\d+)\s*(.*)$")
BENIGN = [r"^10003 file: work/state/.*chkpt\.json - get metadata returned: No such file or directory"]
# network mode: the client disconnects on a restart, and the runner stops OLR with SIGTERM at the end
BENIGN_NETWORK = [r"^10056 host disconnected", r"^10015 caught signal: 15$"]


def check_run(workdir, info, sc):
    problems = []
    log = (workdir / "olr.log").read_text(errors="replace")
    seen = set()
    for line in log.splitlines():
        m = LOG_LINE.match(line)
        if not m:
            continue
        text = f"{m.group(2)} {m.group(3)}"
        expected = [p for p in sc.expect_log if re.search(p, text)]
        seen.update(expected)
        benign = BENIGN + (BENIGN_NETWORK if sc.mode == "network" else [])
        if not expected and not any(re.search(p, text) for p in benign):
            problems.append(f"{m.group(1)} {text}")
    for p in sc.expect_log:
        if p not in seen:
            problems.append(f"expected log line /{p}/ missing")
    if info["timed_out"]:
        problems.append(f"timed out after {info['seconds']}s (did not stop after the last log switch)")
    elif sc.mode != "network" and not sc.stop and not sc.expect_exit and not re.search(r"exhausted number of log switches|shutdown initiated by number of log switches", log):
        problems.append("OLR did not read to the end of the recorded redo")
    code = info["exit_code"]
    if sc.expect_exit == "zero" and code != 0:
        problems.append(f"exit status {code}, expected 0")
    elif sc.expect_exit == "nonzero" and code == 0:
        problems.append("exit status 0 after an error, expected non-zero")
    elif not sc.expect_exit and sc.mode != "network" and code not in (0, None) and not info["timed_out"] and not problems:
        problems.append(f"exit status {code}")
    return problems


def read_events(path, start_scn=None):
    """Returns (events in output order, problems). An event: op, table, before, after, xid, tm (of the
    transaction's begin message). No message may carry an scn below the start SCN: a client which started there
    (Debezium) drops such messages."""
    events, problems, open_xid, tm = [], [], None, None
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
        if start_scn is not None and "scn" in msg and int(msg["scn"]) < start_scn:
            problems.append(f"output line {ln}: scn {msg['scn']} below the start scn {start_scn}")
        for p in msg.get("payload", []):
            op = p.get("op")
            if op == "begin":
                if open_xid is not None:
                    problems.append(f"output line {ln}: transaction {msg.get('xid')} begins inside {open_xid}")
                open_xid, tm = msg.get("xid"), msg.get("tm")
            elif op == "commit":
                open_xid = None
            elif op in ("c", "u", "d"):
                if open_xid is None or msg.get("xid") != open_xid:
                    problems.append(f"output line {ln}: {op} for xid {msg.get('xid')} outside its transaction")
                sch = p.get("schema", {})
                events.append({"op": op, "table": f"{sch.get('owner')}.{sch.get('table')}", "xid": msg.get("xid"),
                               "tm": tm, "before": p.get("before"), "after": p.get("after"), "line": ln})
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


def check_transactions(sc, events):
    """commit_time_column: the begin message's tm (UTC epoch ns, the time of the first change) must be within 5 s
    of the UTC time the workload stored in that column. xid_column: the xid (format 3, LogMiner) must equal the
    V$TRANSACTION.XID the workload stored."""
    problems = []
    for ev in events:
        row = ev["after"] or {}
        if sc.commit_time_column and row.get(sc.commit_time_column) is not None:
            want, got = int(row[sc.commit_time_column]), ev["tm"]
            if got is None or abs(int(got) - want) > 5 * 10**9:
                problems.append(f"output line {ev['line']}: tm {got} is not the UTC time {want} of the change "
                                f"({'missing' if got is None else f'{(int(got) - want) / 1e9:+.0f} s'})")
        if sc.xid_column and row.get(sc.xid_column) is not None:
            if str(ev["xid"]).lower() != str(row[sc.xid_column]).lower():
                problems.append(f"output line {ev['line']}: xid {ev['xid']} != V$TRANSACTION.XID {row[sc.xid_column]}")
    return problems[:10]


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
            rec, info = record(sc, image, workdir)
            if info is None:
                info = run_olr(image, rec, sc, workdir)
            events, parse_problems = read_events(workdir / "output.jsonl", rec["start_scn"])
            problems = check_run(workdir, info, sc) if "run" in sc.checks else []
            if "replay" in sc.checks:
                problems += parse_problems + check_replay(sc, rec, events) + check_transactions(sc, events)
            if "counts" in sc.checks:
                problems += check_counts(sc, events)
        except Exception as e:      # setup/workload errors are test bugs, report them as failures
            problems, events = [f"harness error: {e}"], []
            if (workdir / "olr.log").exists():     # e.g. OLR stopped at startup
                problems += [f"OLR: {m.group(1)} {m.group(2)} {m.group(3)}" for m in
                             map(LOG_LINE.match, (workdir / "olr.log").read_text(errors="replace").splitlines())
                             if m and m.group(1) == "ERROR"][:3]
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
