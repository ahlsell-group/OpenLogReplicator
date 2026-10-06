#!/usr/bin/env python3
"""Generates the restart/ scenarios for the START/CONTINUE-after-log-switch loss.

  python3 fixtures/start-log-switch/gen.py      # regenerate, commit the result

All variants: a 20 000-row INSERT in sequence N, a log switch while the transaction is open, a
500-row INSERT on a 300-column table and the COMMIT in sequence N+1 (one transaction of 20 500
rows). The client stores a position 700 messages into it, OLR gets SIGKILL, the log is switched
(ALTER SYSTEM SWITCH LOGFILE + ARCHIVE LOG CURRENT, 2 or 6 switches: with two online groups the
begin of the transaction is then only in the archive), OLR starts again and the client
reconnects: START with its stored scn when the writer checkpoint was deleted (as an init container
might do before each start), CONTINUE with (c_scn, c_idx) when it was kept.

  chkpt-in-commit-lwn-*   state interval-s 0 and an idle pause after the COMMIT: OLR writes a
                          schema checkpoint right after the LWN with the COMMIT. The commit
                          record carries an SCN above the SCN of its LWN, so that checkpoint is
                          at or below the commit SCN the client restarts from, and it has no
                          min-tran (the transaction is committed). This is the trigger.
  switch-after-kill-*     control: default interval, OLR has caught up before each log switch,
                          so no schema checkpoint is written after the COMMIT LWN; only the log
                          switches after the kill differ from a plain restart.
"""
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = ROOT / "scenarios" / "restart"

SETUP = """BEGIN EXECUTE IMMEDIATE 'DROP USER {u} CASCADE';
EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1918 THEN RAISE; END IF; END;
/
CREATE USER {u} IDENTIFIED BY olrt QUOTA UNLIMITED ON users DEFAULT TABLESPACE users;
CREATE TABLE {u}.t (id NUMBER(10) PRIMARY KEY, v VARCHAR2(40), q NUMBER(15,3), d DATE);
ALTER TABLE {u}.t ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
DECLARE s VARCHAR2(32767) := 'CREATE TABLE {u}.w (id NUMBER(10) PRIMARY KEY';
BEGIN
  FOR i IN 2..300 LOOP s := s || ', c' || i || CASE MOD(i, 3) WHEN 0 THEN ' DATE' WHEN 1 THEN ' NUMBER(15,3)' ELSE ' VARCHAR2(20)' END; END LOOP;
  EXECUTE IMMEDIATE s || ')';
END;
/
ALTER TABLE {u}.w ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
INSERT INTO {u}.t VALUES (1, 'initial', 0, DATE '2026-01-01');
INSERT INTO {u}.w (id, c2, c255, c256, c299, c300) VALUES (1, 'initial', DATE '2026-01-01', 1, 'x', DATE '2026-01-01');
COMMIT;
"""

KNOWN = ("checkpoint after the COMMIT LWN has no min-tran for a transaction committed at or above its SCN;"
         " START/CONTINUE from the commit SCN skips the transaction (fork fix/checkpoint-min-tran-commit-lwn)")


def step(comment=None, **kw):
    out = [f"# {comment}"] if comment else []
    out.append("[[step]]")
    for k, v in kw.items():
        if isinstance(v, bool):
            v = "true" if v else "false"
        elif isinstance(v, str):
            v = f'"{v}"'
        out.append(f"{k} = {v}")
    return "\n".join(out)


def client_toml(u, trigger, delete, hard):
    t = u.lower()
    olr = "[olr]\nqueue_size = 65536\n" + ("state_interval_s = 0\n" if trigger else "")
    steps = [
        step(client="connect"),
        step(sql=f"UPDATE {t}.t SET v = 'warmup' WHERE id = 1"), step(sql="COMMIT"),
        step(read="commits", count=1), step(client="commit_offset"),
        step(sql=f"INSERT INTO {t}.t SELECT 1000 + LEVEL, 'big-' || LEVEL, LEVEL / 1000, DATE '2026-01-01' + MOD(LEVEL, 365)"
                 " FROM dual CONNECT BY LEVEL <= 20000"),
        step("OLR catches up first, so the checkpoint written on the switch lies inside the transaction", sleep=3),
        step(switch_logfile=True), step(sleep=3),
        step(sql=f"INSERT INTO {t}.w (id, c2, c255, c256, c257, c299, c300) SELECT 1000 + LEVEL, 'w' || LEVEL,"
                 " DATE '2027-01-01', LEVEL / 7, 'p2-' || LEVEL, 'hi-' || LEVEL, DATE '2028-01-01' + LEVEL"
                 " FROM dual CONNECT BY LEVEL <= 500"),
        step(sql="COMMIT"),
        step("idle: OLR parses the COMMIT LWN" + (" and checkpoints right after it" if trigger else ""), sleep=3),
        step(read="messages", count=700, timeout=60), step(client="commit_offset"),
        step(client="disconnect"),
        step(olr="stop", signal="KILL", delete_writer_checkpoint=delete),
        step(f"{2 * hard} log switches while OLR is down", switch_logfile=True, hard=True, count=hard),
        step(olr="start"),
        step(client="connect"), step(read="idle", idle=10), step(client="commit_offset"),
        step(sql=f"INSERT INTO {t}.t VALUES (2, 'after', 2, DATE '2026-02-02')"), step(sql="COMMIT"),
        step(read="commits", count=1),
    ]
    return olr + "\n" + "\n".join(steps) + "\n"


VARIANTS = [
    # name, user, trigger, writer checkpoint deleted, hard switch pairs after the kill
    ("chkpt-in-commit-lwn-start", "OLRT_RSL1", True, True, 1),
    ("chkpt-in-commit-lwn-continue", "OLRT_RSL2", True, False, 1),
    ("chkpt-in-commit-lwn-archived-start", "OLRT_RSL3", True, True, 3),
    ("chkpt-in-commit-lwn-archived-continue", "OLRT_RSL4", True, False, 3),
    ("switch-after-kill-start", "OLRT_RSL5", False, True, 3),
    ("switch-after-kill-continue", "OLRT_RSL6", False, False, 3),
]


def main():
    for name, u, trigger, delete, hard in VARIANTS:
        d = OUT / name
        d.mkdir(parents=True, exist_ok=True)
        path = "START (writer checkpoint deleted)" if delete else "CONTINUE (writer checkpoint kept)"
        what = ("schema checkpoint right after the COMMIT LWN" if trigger
                else "control, no schema checkpoint after the COMMIT LWN")
        toml = [
            f'description = "SIGKILL 700 rows into a 20 500-row transaction over a log switch, {2 * hard} log switches'
            f' before the restart, {path}; {what}"',
            'tags = ["restart", "network", "start-log-switch"]',
            f'tables = ["{u}.T", "{u}.W"]',
            "olr_timeout = 900",
            'allowed_warnings = ["^10061 network error, errno: 32"]   # the client disconnects on purpose while OLR is sending',
        ]
        if trigger and delete:
            toml += ["", "[known_issue]", f'run = "{KNOWN}"', f'diff = "{KNOWN}"', f'replay = "{KNOWN}"']
        (d / "scenario.toml").write_text("\n".join(toml) + "\n")
        (d / "setup.sql").write_text(SETUP.format(u=u))
        (d / "workload.sql").write_text("-- network mode: the workload is in client.toml\n")
        (d / "client.toml").write_text(client_toml(u, trigger, delete, hard))
        readme = [
            f"Generated by fixtures/start-log-switch/gen.py (see there for the shared steps).",
            "",
            f"Restart path: {path}. Log switches between the kill and the restart: {2 * hard}"
            + (" (the begin of the transaction is only in the archive)." if hard > 1 else "."),
            "",
        ]
        if not delete:
            readme.append(
                "CONTINUE uses the writer checkpoint (`<db>-chkpt.json`) as start SCN. Here it still holds an SCN"
                " from before the transaction (the writer was blocked sending when the CONFIRM came), so OLR starts"
                " from an older schema checkpoint and the transaction arrives; the CONTINUE path is exposed only"
                " when the writer checkpoint holds the commit SCN. Images without the c_idx fix (fork 2dfbd467) lose one"
                " row here: F1 in docs/findings-adversarial.md, the c_idx off-by-one (upstream #325).")
            readme.append("")
        if trigger:
            readme.append(
                "`state.interval-s` is 0 and the database is idle after the COMMIT, so OLR writes a schema checkpoint right"
                " after the LWN holding the COMMIT. Its SCN is the SCN of that LWN, which can be below the commit SCN. On"
                " restart from the commit SCN, images without fork fix/checkpoint-min-tran-commit-lwn take that checkpoint and"
                " start behind the COMMIT: the whole transaction is lost (run fails: no commit for the transaction; diff"
                " and replay miss 20 500 rows).")
        else:
            readme.append(
                "Control: the default checkpoint interval and OLR caught up before each log switch, so no schema"
                " checkpoint is written after the COMMIT LWN; the restart starts at the checkpoint inside the"
                " transaction (min-tran) and the transaction arrives in full.")
        (d / "README.md").write_text("\n".join(readme) + "\n")


if __name__ == "__main__":
    main()
