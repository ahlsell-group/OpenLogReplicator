"""Phase 1: run each scenario against the local Oracle Free and record the ground truth.

For every scenario: start SCN, "before" snapshot AS OF the start SCN, workload, end SCN,
"after" snapshot AS OF the end SCN, archived redo range, and the LogMiner reference over
exactly that SCN range. Everything goes to results/<run>/record/<scenario>/recording.json.
"""
import json
import time

import oracledb

from . import db, logminer, normalize, scenario as scn_mod, sqlscript
from . import settings as S


def log(msg):
    print(f"[record] {msg}", flush=True)


def run_script(text, sessions, root_conn, label, connect=db.pdb):
    """Execute a scenario SQL script. sessions: dict id -> connection (opened lazily)."""
    out = []
    current = 1
    expect = None
    for step in sqlscript.parse(text):
        if step.kind == "session":
            current = int(step.arg)
            continue
        if step.kind == "switch_logfile":
            db.switch_logfile(root_conn)
            out.append(f"{label}:{step.line}: log switch")
            continue
        if step.kind == "sleep":
            time.sleep(float(step.arg))
            continue
        if step.kind == "expect_error":
            expect = step.arg.strip()
            continue
        if step.kind != "sql":
            raise ValueError(f"{label}:{step.line}: unknown directive @{step.kind}")
        conn = sessions.get(current)
        if conn is None:
            conn = sessions[current] = connect()
        cur = conn.cursor()
        try:
            stmt = step.arg
            upper = stmt.strip().upper()
            if upper == "COMMIT":
                conn.commit()
            elif upper == "ROLLBACK":
                conn.rollback()
            else:
                cur.execute(stmt)
            if expect:
                raise RuntimeError(f"{label}:{step.line}: expected {expect}, statement succeeded")
            rc = cur.rowcount if cur.rowcount is not None else -1
            out.append(f"{label}:{step.line}: s{current} ok rows={rc}")
        except oracledb.DatabaseError as e:
            msg = str(e).splitlines()[0]
            if expect and expect in msg:
                out.append(f"{label}:{step.line}: s{current} expected error {msg}")
            else:
                raise RuntimeError(f"{label}:{step.line}: {msg}\n{step.arg[:400]}") from None
        expect = None
    return out


def table_info(conn, owner_table, scenario):
    owner, table = owner_table.split(".")
    cols = conn.cursor().execute(
        """SELECT column_name, data_type, data_precision, data_scale, char_length, column_id
             FROM dba_tab_cols
            WHERE owner = :o AND table_name = :t AND hidden_column = 'NO' AND virtual_column = 'NO'
            ORDER BY column_id""", o=owner, t=table).fetchall()
    if not cols:
        return None
    # object id and column ids survive RENAME TABLE / RENAME COLUMN: the replay check maps names by them
    obj = conn.cursor().execute(
        """SELECT object_id FROM dba_objects WHERE owner = :o AND object_name = :t AND object_type = 'TABLE'""",
        o=owner, t=table).fetchone()
    key = [r[0] for r in conn.cursor().execute(
        """SELECT cc.column_name FROM dba_constraints c
             JOIN dba_cons_columns cc ON cc.owner = c.owner AND cc.constraint_name = c.constraint_name
            WHERE c.owner = :o AND c.table_name = :t AND c.constraint_type = 'P'
            ORDER BY cc.position""", o=owner, t=table).fetchall()]
    key = scenario.keys.get(owner_table, key)
    return {
        "columns": [{"name": n, "type": t, "precision": None if p is None else int(p),
                     "scale": None if s is None else int(s), "length": None if l is None else int(l),
                     "column_id": None if i is None else int(i)}
                    for n, t, p, s, l, i in cols],
        "key": key or [c[0] for c in cols],
        "object_id": int(obj[0]) if obj else None,
    }


def snapshot(conn, owner_table, info, scn):
    if info is None:
        return []
    owner, table = owner_table.split(".")
    exprs = ", ".join(normalize.select_expr(c["name"], c["type"]) for c in info["columns"])
    try:
        rows = conn.cursor().execute(f'SELECT {exprs} FROM "{owner}"."{table}" AS OF SCN {int(scn)}').fetchall()
    except oracledb.DatabaseError as e:
        # ORA-01466: DDL on the table within the SCN-to-time granularity (a few seconds).
        # Snapshots are taken right at the SCN with nothing else running, so a plain
        # query sees the same data.
        if "ORA-01466" not in str(e):
            raise
        rows = conn.cursor().execute(f'SELECT {exprs} FROM "{owner}"."{table}"').fetchall()
    out = []
    for r in rows:
        out.append({c["name"]: normalize.canon(v, c["type"]) for c, v in zip(info["columns"], r)})
    return out


def apply_fixtures(sc, pconn, rconn, token):
    cur = pconn.cursor()
    try:
        cur.execute("CREATE TABLE system.olrt_fixtures (name VARCHAR2(100) PRIMARY KEY, sha VARCHAR2(64))")
    except oracledb.DatabaseError as e:
        if "ORA-00955" not in str(e):
            raise
    for name in sc.fixtures:
        text, sha = scn_mod.fixture_sql(name)
        sha = f"{sha}:{token}"  # fixtures are rebuilt once per recording run, so reruns start clean
        row = cur.execute("SELECT sha FROM system.olrt_fixtures WHERE name = :n", n=name).fetchone()
        if row and row[0] == sha:
            continue
        log(f"fixture {name} ({sha})")
        t0 = time.time()
        run_script(text, {1: pconn}, rconn, f"fixtures/{name}")
        cur.execute("DELETE FROM system.olrt_fixtures WHERE name = :n", n=name)
        cur.execute("INSERT INTO system.olrt_fixtures VALUES (:n, :s)", n=name, s=sha)
        pconn.commit()
        log(f"fixture {name} done in {time.time() - t0:.0f}s")


def record(sc, outdir, token="adhoc"):
    outdir.mkdir(parents=True, exist_ok=True)
    rconn, pconn = db.root(), db.scenario_conn(sc)
    sessions = {1: pconn}
    connect = lambda: db.scenario_conn(sc)
    try:
        apply_fixtures(sc, pconn, rconn, token)
        setup_log = run_script(sc.sql("setup.sql"), sessions, rconn, "setup.sql", connect) if sc.sql("setup.sql") else []
        pconn.commit()
        db.set_force_logging(rconn, sc.force_logging)

        dict_seq = None
        if sc.logminer_dict == "redo":
            # LogMiner reference with a dictionary in the redo, so DDL inside the range is
            # tracked (DICT_FROM_ONLINE_CATALOG cannot decode redo written before a DDL)
            db.switch_logfile(rconn)
            dict_seq = db.current_seq(rconn)
            rconn.cursor().execute("BEGIN DBMS_LOGMNR_D.BUILD(OPTIONS => DBMS_LOGMNR_D.STORE_IN_REDO_LOGS); END;")
        db.switch_logfile(rconn)
        start_seq = db.current_seq(rconn)
        start_scn = db.current_scn(rconn)
        tables_before = {t: table_info(pconn, t, sc) for t in sc.tables}
        before = {t: snapshot(pconn, t, tables_before[t], start_scn) for t in sc.tables}

        t0 = time.time()
        work_log = run_script(sc.sql("workload.sql"), sessions, rconn, "workload.sql", connect)
        for conn in sessions.values():
            conn.commit()  # a workload must end committed; this is a no-op when it did
        workload_s = time.time() - t0

        end_scn = db.current_scn(rconn)
        db.switch_logfile(rconn)
        end_seq = db.current_seq(rconn) - 1
        tables_after = {t: table_info(pconn, t, sc) for t in sc.tables}
        after = {t: snapshot(pconn, t, tables_after[t], end_scn) for t in sc.tables}
        archived = db.archived_logs(rconn, start_seq, end_seq)
        if [a[0] for a in archived] != list(range(start_seq, end_seq + 1)):
            raise RuntimeError(f"archived redo incomplete: want {start_seq}..{end_seq}, have {[a[0] for a in archived]}")
        lm_logs = archived if dict_seq is None else db.archived_logs(rconn, dict_seq, end_seq)
        # mine every column that existed at either end (a column SET UNUSED in the range
        # is only in tables_before)
        lm_tables = {}
        for t in sc.tables:
            a, b = tables_after.get(t), tables_before.get(t)
            if a and b:
                names = {c["name"] for c in a["columns"]}
                a = dict(a, columns=a["columns"] + [c for c in b["columns"] if c["name"] not in names])
            lm_tables[t] = a or b
        reference = logminer.mine(rconn, lm_logs, start_scn, end_scn, lm_tables,
                                  dictionary=sc.logminer_dict, con_name=sc.con_name)
        dbtz = db.scalar(rconn, "SELECT TO_CHAR(SYSTIMESTAMP, 'TZR') FROM dual")
        rec = {
            "id": sc.id,
            "start_scn": start_scn, "end_scn": end_scn,
            "start_seq": start_seq, "end_seq": end_seq,
            "archived": [{"seq": a[0], "name": a[1], "first_scn": a[2], "next_scn": a[3]} for a in archived],
            "db_tz": dbtz, "host_tz": S.ORACLE_TZ,
            "workload_seconds": round(workload_s, 2),
            "tables_before": tables_before, "tables": tables_after,
            "before": before, "after": after,
            "logminer": reference,
            "setup_log": setup_log, "workload_log": work_log,
        }
        (outdir / "recording.json").write_text(json.dumps(rec, indent=1, ensure_ascii=False))
        ntx = len(reference["transactions"])
        nev = sum(len(t["events"]) for t in reference["transactions"])
        log(f"{sc.id}: scn {start_scn}..{end_scn} seq {start_seq}..{end_seq}, "
            f"logminer {ntx} txn / {nev} rows")
        return rec
    finally:
        for c in sessions.values():
            try:
                c.rollback()
                c.close()
            except oracledb.Error:
                pass
        rconn.close()
