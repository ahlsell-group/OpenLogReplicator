"""Network-writer mode: OLR runs live with the network writer and a Debezium-like client
(olrt.netclient) drives it, interleaved with SQL. Used for client restart / reconnect
semantics that the file writer cannot show (e.g. bersler/OpenLogReplicator#330).

A scenario opts in with a client.toml next to scenario.toml (scenario.toml itself is
unchanged). Each such scenario is recorded per image x profile, because the workload has
to run while OLR is connected; the result directory gets the same files as the file mode
(recording.json, output.jsonl, olr.log, run.json), so olrt.checks works unchanged.

client.toml:

  [olr]
  state_interval_s = 600          # OLR checkpoint interval (state.interval-s)
  state_interval_mb = 0           # optional state.interval-mb (2.0.0: 0 = checkpoint every loop)
  keep_checkpoints = 100          # optional state.keep-checkpoints
  stop_signal = "INT"             # signal for the final stop ("TERM" = docker stop / pod stop)
  queue_size = 65536              # network writer queue-size
  trace = 0                       # OLR "trace" bit mask (e.g. 16384 = CHECKPOINT)

  [[step]]
  client = "connect"              # Debezium connect: INFO, then START(offset scn) if READY or
                                  # CONTINUE(c_scn, c_idx) if REPLICATE. Initial offset is the
                                  # start SCN with no index (like after a snapshot).
  offset = "current"              # optional: offset := current SCN first (snapshot taken now)
  [[step]]
  sql = "INSERT INTO t VALUES (1)"  # one statement or PL/SQL block; COMMIT/ROLLBACK allowed
  session = 2                     # default 1; sessions stay open across steps
  [[step]]
  read = "commits"                # read data messages until `count` commit messages arrived
  count = 1
  timeout = 30                    # seconds (default 30); missing data -> run.json "read_timeouts"
  [[step]]
  read = "messages"               # read until `count` data messages (begin/DML/commit) arrived,
  count = 1000                    # e.g. to disconnect in the middle of a large transaction
  [[step]]
  read = "idle"                   # read until no message for `idle` seconds (default 5)
  idle = 5
  [[step]]
  client = "commit_offset"        # offset := (c_scn, c_idx) of the last received message, then
                                  # confirm() with Debezium's rule (only if prev c_scn < c_scn)
  [[step]]
  client = "confirm_last"         # raw CONFIRM of the last received c_scn/c_idx (no rule)
  [[step]]
  client = "disconnect"
  [[step]]
  olr = "restart"                 # docker stop + start, same state dir
  delete_writer_checkpoint = true # remove <db>-chkpt.json first (as an init container might)
  signal = "INT"                  # INT = clean shutdown, KILL = crash
  [[step]]
  olr = "stop"                    # as restart, but OLR stays down until an olr = "start" step
  [[step]]                        # (log switches while OLR is down)
  olr = "start"
  [[step]]
  mark = "long"                   # remember the current SCN and the open transaction of `session`
  session = 2                     # (xid as OLR prints it) under this name
  [[step]]
  checkpoints = "before-restart"  # list OLR's metadata checkpoints (<db>-chkpt-<scn>.json) into
  covers = "long"                 # run.json; with covers: fail "run" unless a kept checkpoint at or
                                  # below the client's offset SCN has min-tran = that transaction (or
                                  # one open at the mark that began earlier), at or before the log
                                  # sequence of the mark (restart replays it whole)
  [[step]]
  switch_logfile = true           # ARCHIVE LOG CURRENT (one switch, waits for the archive)
  count = 1                       # optional: repeat
  hard = true                     # optional: ALTER SYSTEM SWITCH LOGFILE first, then
                                  # ARCHIVE LOG CURRENT (two switches per count)
  [[step]]
  sleep = 2

An olr restart/stop step also records the state directory (schema checkpoints) in run.json, so a
run shows which <db>-chkpt-<scn>.json OLR could pick on restart.

After the last step: if the client is connected, a final read = "idle"; then the end SCN
is taken, the log switched, snapshots and LogMiner as in record.py, and OLR is stopped.
"""
import json
import os
import pathlib
import re
import shutil
import socket
import subprocess
import time
import tomllib

from . import db, logminer, netclient, olr, record
from . import settings as S

NO_NETWORK = 'not "network" since the code is not compiled'


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def load_steps(scenario):
    p = scenario.path / "client.toml"
    if not p.exists():
        return None
    return tomllib.loads(p.read_text())


def make_config(scenario, profile, olr_opts, version=(2, 0)):
    stub = {"start_scn": 0, "start_seq": 0, "end_seq": 0}
    cfg = olr.make_config(stub, scenario, profile, version=version)
    src = cfg["source"][0]
    src["reader"].pop("start-scn", None)     # the client's START decides
    src.pop("debug", None)                   # runs live, stopped by us
    state = cfg.get("state") or src["state"]
    state["interval-s"] = int(olr_opts.get("state_interval_s", 600))
    if "state_interval_mb" in olr_opts:
        state["interval-mb"] = int(olr_opts["state_interval_mb"])
    if "keep_checkpoints" in olr_opts:
        state["keep-checkpoints"] = int(olr_opts["keep_checkpoints"])
    if "trace" in olr_opts:
        cfg["trace"] = int(olr_opts["trace"])
    cfg["target"][0]["alias"] = "NET"
    cfg["target"][0]["writer"] = {"type": "network", "uri": "0.0.0.0:5000",
                                  "poll-interval-us": 1000,
                                  "queue-size": int(olr_opts.get("queue_size", 65536))}
    # continue-gap (fork option): 1 = refuse a CONTINUE before the confirmed position whose messages were released
    gap = olr_opts.get("continue_gap", os.environ.get("OLRT_OLR_CONTINUE_GAP", ""))
    if gap != "":
        cfg["target"][0]["writer"]["continue-gap"] = int(gap)
    # OLRT_OLR_TRACE: OLR trace mask, e.g. 8192 writer + 16384 checkpoint + 524288 stream requests/data
    trace = int(olr_opts.get("trace", os.environ.get("OLRT_OLR_TRACE", "0") or 0))
    if trace:
        cfg["trace"] = trace
    return cfg


class OlrContainer:
    def __init__(self, image, workdir, port):
        self.image, self.workdir, self.port = image, workdir, port
        self.name = "olrt-olrnet-" + str(abs(hash(str(workdir))))[:12]

    def start(self):
        subprocess.run(["docker", "rm", "-f", self.name], capture_output=True)
        subprocess.run(["docker", "run", "-d", "--name", self.name, "--network", S.NETWORK,
                        "--user", f"{os.getuid()}:{S.ORACLE_GID}",
                        "-p", f"127.0.0.1:{self.port}:5000",
                        "-v", f"{S.VOLUME}:/opt/oracle/oradata:ro",
                        "-v", f"{self.workdir.resolve()}:/opt/OpenLogReplicator/work",
                        "--label", "olrt=1",
                        "--entrypoint", "/opt/OpenLogReplicator/OpenLogReplicator",
                        self.image, "-f", "work/config.json"], check=True, capture_output=True)

    def status(self):
        out = subprocess.run(["docker", "inspect", "-f", "{{.State.Status}} {{.State.ExitCode}}", self.name],
                             capture_output=True, text=True).stdout.split()
        return (out[0], int(out[1])) if out else ("gone", None)

    def logs(self):
        r = subprocess.run(["docker", "logs", self.name], capture_output=True, text=True)
        return r.stdout + r.stderr

    def stop(self, signal="INT"):
        # OLR handles SIGINT only; SIGTERM is ignored as PID 1, so a plain `docker stop`
        # (and a Kubernetes pod stop) ends in SIGKILL after the grace period.
        subprocess.run(["docker", "stop", "-s", f"SIG{signal}", "-t", "20", self.name], capture_output=True)
        return self.status()[1]

    def restart(self, signal="INT"):
        self.stop(signal)
        subprocess.run(["docker", "start", self.name], check=True, capture_output=True)

    def remove(self):
        subprocess.run(["docker", "rm", "-f", self.name], capture_output=True)


def olr_xid(local_transaction_id):
    """DBMS_TRANSACTION.LOCAL_TRANSACTION_ID ("usn.slot.sqn", decimal) as OLR prints an xid."""
    usn, slt, sqn = (int(x) for x in local_transaction_id.split("."))
    return f"0x{usn:04x}.{slt:03x}.{sqn:08x}"


def list_checkpoints(state_dir):
    """OLR's metadata checkpoints in the state dir: [{scn, seq, offset, min_tran}], oldest first."""
    out = []
    for p in state_dir.glob(f"{S.PDB}-chkpt-*.json"):
        try:
            scn = int(p.stem.rsplit("-", 1)[1])
            # only the header is needed; the schema part can be tens of MB
            head = p.read_bytes()[:4096].decode(errors="replace")
        except (ValueError, OSError):
            continue
        rec = {"scn": scn, "file": p.name, "bytes": p.stat().st_size}
        for key in ("seq", "offset"):
            m = re.search(rf'"{key}":(\d+)', head)
            rec[key] = int(m.group(1)) if m else None
        m = re.search(r'"min-tran":\{"seq":(\d+),"offset":(\d+),"xid":"([^"]+)"\}', head)
        rec["min_tran"] = {"seq": int(m.group(1)), "offset": int(m.group(2)), "xid": m.group(3)} if m else None
        out.append(rec)
    return sorted(out, key=lambda r: r["scn"])


def check_coverage(cps, mark, offset_scn):
    """Problems (empty = fine) for: a checkpoint at or below the restart SCN exists and its
    min-tran is the marked open transaction, starting no later than the mark's log sequence."""
    usable = [c for c in cps if c["scn"] <= offset_scn]
    if not usable:
        return [f"no metadata checkpoint at or below the client offset SCN {offset_scn}: kept "
                f"{len(cps)} checkpoint(s), SCN {cps[0]['scn'] if cps else '-'}..{cps[-1]['scn'] if cps else '-'}"]
    c = usable[-1]
    mt = c["min_tran"]
    if mt is None:
        return [f"checkpoint {c['scn']} (the one a restart at {offset_scn} reads) has no min-tran, "
                f"open transaction {mark['xid']} would be lost"]
    probs = []
    if mt["xid"] != mark["xid"]:
        # another transaction may be older (e.g. a background one); OLR then restarts at that one,
        # which still covers ours, as long as it was already open at the mark and began first
        other = (mark.get("open") or {}).get(mt["xid"])
        mine = (mark.get("open") or {}).get(mark["xid"])
        if other is None or mine is None or other > mine:
            probs.append(f"checkpoint {c['scn']}: min-tran xid {mt['xid']} (start SCN {other}) is not the open "
                         f"transaction {mark['xid']} (start SCN {mine}) or an older one")
    if mt["seq"] > mark["seq"]:
        probs.append(f"checkpoint {c['scn']}: min-tran seq {mt['seq']} is after the open transaction's start (seq {mark['seq']})")
    return probs


def run(image, scenario, profile, workdir, token="adhoc"):
    """Record + replicate one network-mode scenario. Returns the run.json dict."""
    spec = load_steps(scenario)
    if spec is None:
        raise ValueError(f"{scenario.id}: no client.toml")
    steps = spec.get("step", [])
    if workdir.exists():
        shutil.rmtree(workdir)
    (workdir / "state").mkdir(parents=True)
    os.chmod(workdir, 0o777)
    os.chmod(workdir / "state", 0o777)
    (workdir / "config.json").write_text(json.dumps(
        make_config(scenario, profile, spec.get("olr", {}), version=olr.olr_version(image)), indent=2))

    events = []          # client-side log, for evidence
    run_info = {"image": image, "profile": profile, "mode": "network", "exit_code": None,
                "timed_out": False, "skip": None, "read_timeouts": 0, "log": "olr.log",
                "output": "output.jsonl", "assert_failures": [], "checkpoints": []}
    marks = {}           # mark step name -> {scn, seq, xid}
    start_offsets = []   # SCNs the client sent START with
    out = open(workdir / "output.jsonl", "w")
    rawlog = open(workdir / "received.jsonl", "w")   # everything OLR sent, incl. client-skipped
    rconn, pconn = db.root(), db.scenario_conn(scenario)
    sessions = {1: pconn}
    port = free_port()
    cont = OlrContainer(image, workdir, port)
    client = None
    requests = []        # every request sent by any client instance
    offset = None        # (scn, idx) as Debezium would store it
    last = None          # (c_scn, c_idx) of the last received message
    t0 = time.time()

    def read(mode, step):
        nonlocal last
        commits = 0
        want = int(step.get("count", 1))
        idle = float(step.get("idle", 5))
        deadline = time.time() + float(step.get("timeout", 30))
        while time.time() < deadline:
            got = client.read_event(timeout=idle if mode == "idle" else min(2.0, max(0.1, deadline - time.time())))
            if got is None:
                if mode == "idle":
                    return
                continue
            raw, msg, skipped = got
            for s in skipped:
                rawlog.write(json.dumps({"skipped_by_client": True, "raw": s}) + "\n")
            if raw is None:
                continue
            rawlog.write(raw + "\n")
            out.write(raw + "\n")
            out.flush()
            if "c_scn" in msg:
                last = (int(msg["c_scn"]), int(msg["c_idx"]) if "c_idx" in msg else None)
            if any(p.get("op") == "commit" for p in msg.get("payload", [])):
                commits += 1
                if mode == "commits" and commits >= want:
                    return
            if mode == "messages" and any(p.get("op") != "chkpt" for p in msg.get("payload", [])):
                commits += 1
                if commits >= want:
                    return
        if mode in ("commits", "messages"):
            run_info["read_timeouts"] += 1
            events.append(f"read: timeout, {commits}/{want} commits")

    try:
        record.apply_fixtures(scenario, pconn, rconn, token)
        if scenario.sql("setup.sql"):
            record.run_script(scenario.sql("setup.sql"), sessions, rconn, "setup.sql", lambda: db.scenario_conn(scenario))
        pconn.commit()
        db.set_force_logging(rconn, scenario.force_logging)
        db.switch_logfile(rconn)
        start_seq = db.current_seq(rconn)
        start_scn = db.current_scn(rconn)
        tables_before = {t: record.table_info(pconn, t, scenario) for t in scenario.tables}
        before = {t: record.snapshot(pconn, t, tables_before[t], start_scn) for t in scenario.tables}
        offset = (start_scn, None)

        cont.start()
        time.sleep(2)
        st, rc = cont.status()
        if st != "running":
            logs = cont.logs()
            if NO_NETWORK in logs:
                run_info["skip"] = "image built without protobuf: no network writer"
            run_info["exit_code"] = rc
            raise _Abort()

        for i, step in enumerate(steps):
            if "sql" in step:
                stmt = step["sql"].strip()
                if stmt.upper().startswith(("BEGIN", "DECLARE")):
                    if not stmt.endswith("/"):
                        stmt += "\n/"          # a PL/SQL block ending in "END;" still needs the "/"
                elif not stmt.endswith(";"):
                    stmt += ";"
                sid = int(step.get("session", 1))
                if sid not in sessions:
                    sessions[sid] = db.scenario_conn(scenario)
                text = (f"-- @session {sid}\n" if sid != 1 else "") + stmt + "\n"
                events.append(f"step {i}: sql s{sid}: {step['sql'][:80]}")
                record.run_script(text, sessions, rconn, f"client.toml step {i}")
            elif step.get("client") == "connect":
                if step.get("offset") == "current":
                    # START at "now", like Debezium after a snapshot taken at this SCN
                    offset = (db.current_scn(rconn), None)
                client = netclient.Client(scenario.db_name)
                client.sent = requests
                path, code = client.connect("127.0.0.1", port, offset[0], offset[1])
                events.append(f"step {i}: connect offset={offset} -> {path} -> {code}")
                if path == "START":
                    start_offsets.append(offset[0])
            elif "read" in step:
                read(step["read"], step)
                events.append(f"step {i}: read {step['read']} -> last c_scn/c_idx {last}")
            elif step.get("client") == "commit_offset":
                if last:
                    offset = last
                    sent = client.confirm(last[0], last[1])
                    events.append(f"step {i}: offset={offset}, CONFIRM {'sent' if sent else 'not sent (Debezium rule)'}")
            elif step.get("client") == "confirm_last":
                if last:
                    client.confirm_raw(*last)
                    events.append(f"step {i}: raw CONFIRM {last}")
            elif step.get("client") == "disconnect":
                client.close()
                client = None
                events.append(f"step {i}: disconnect")
            elif step.get("olr") in ("restart", "stop"):
                # stop first: OLR writes <db>-chkpt.json while shutting down, so a delete
                # (e.g. by an init container) happens between stop and start
                cont.stop(step.get("signal", "INT"))
                state_files = sorted(f.name for f in (workdir / "state").iterdir())
                events.append(f"step {i}: OLR stopped ({step.get('signal', 'INT')}), state: {' '.join(state_files)}")
                if step.get("delete_writer_checkpoint"):
                    p = workdir / "state" / f"{scenario.db_name}-chkpt.json"
                    if p.exists():
                        events.append(f"step {i}: deleted {p.name}: {p.read_text().strip()}")
                        p.unlink()
                if step["olr"] == "restart":
                    subprocess.run(["docker", "start", cont.name], check=True, capture_output=True)
                    time.sleep(2)
                    events.append(f"step {i}: OLR restarted")
            elif step.get("olr") == "start":
                subprocess.run(["docker", "start", cont.name], check=True, capture_output=True)
                time.sleep(2)
                events.append(f"step {i}: OLR started")
            elif "mark" in step:
                sid = int(step.get("session", 1))
                ltid = db.scalar(sessions[sid], "SELECT DBMS_TRANSACTION.LOCAL_TRANSACTION_ID FROM dual")
                if not ltid:
                    raise ValueError(f"client.toml step {i}: session {sid} has no open transaction to mark")
                open_tx = {f"0x{int(u):04x}.{int(sl):03x}.{int(sq):08x}": int(st) for u, sl, sq, st in
                           rconn.cursor().execute("SELECT xidusn, xidslot, xidsqn, start_scn FROM v$transaction")}
                marks[step["mark"]] = {"scn": db.current_scn(rconn), "seq": db.current_seq(rconn),
                                       "xid": olr_xid(ltid), "open": open_tx}
                events.append(f"step {i}: mark {step['mark']} = {marks[step['mark']]}")
            elif "checkpoints" in step:
                cps = list_checkpoints(workdir / "state")
                snap = {"label": step["checkpoints"], "offset": offset, "count": len(cps),
                        "scn_min": cps[0]["scn"] if cps else None, "scn_max": cps[-1]["scn"] if cps else None,
                        "files": cps}
                if step.get("covers"):
                    probs = check_coverage(cps, marks[step["covers"]], offset[0])
                    snap["problems"] = probs
                    run_info["assert_failures"] += [f"checkpoints {step['checkpoints']}: {p}" for p in probs]
                run_info["checkpoints"].append(snap)
                events.append(f"step {i}: checkpoints {step['checkpoints']}: {len(cps)} kept, "
                              f"SCN {snap['scn_min']}..{snap['scn_max']}, offset {offset}"
                              + (f", problems: {snap['problems']}" if snap.get("problems") else ""))
            elif step.get("switch_logfile"):
                for _ in range(int(step.get("count", 1))):
                    if step.get("hard"):
                        rconn.cursor().execute("ALTER SYSTEM SWITCH LOGFILE")
                    db.switch_logfile(rconn)
                events.append(f"step {i}: log switch x{step.get('count', 1)}{' (hard)' if step.get('hard') else ''},"
                              f" current seq {db.current_seq(rconn)}")
            elif "sleep" in step:
                time.sleep(float(step["sleep"]))
            else:
                raise ValueError(f"client.toml step {i}: unknown step {step}")

        for c in sessions.values():
            c.commit()
        if client is not None:
            read("idle", {"idle": 5})
        end_scn = db.current_scn(rconn)
        db.switch_logfile(rconn)
        end_seq = db.current_seq(rconn) - 1
        tables_after = {t: record.table_info(pconn, t, scenario) for t in scenario.tables}
        after = {t: record.snapshot(pconn, t, tables_after[t], end_scn) for t in scenario.tables}
        archived = db.archived_logs(rconn, start_seq, end_seq)
        reference = logminer.mine(rconn, archived, start_scn, end_scn, tables_after, con_name=scenario.con_name)
        rec = {
            "id": scenario.id, "mode": "network",
            "start_scn": start_scn, "end_scn": end_scn, "start_seq": start_seq, "end_seq": end_seq,
            "archived": [{"seq": a[0], "name": a[1], "first_scn": a[2], "next_scn": a[3]} for a in archived],
            "db_tz": db.scalar(rconn, "SELECT TO_CHAR(SYSTIMESTAMP, 'TZR') FROM dual"), "host_tz": S.ORACLE_TZ,
            "tables_before": tables_before, "tables": tables_after, "before": before, "after": after,
            "logminer": reference, "start_offsets": start_offsets,
        }
        (workdir / "recording.json").write_text(json.dumps(rec, indent=1, ensure_ascii=False))
        # the file-mode checks expect OLR to stop by itself at the end of the range
        run_info["live"] = True
    except _Abort:
        pass
    finally:
        if client is not None:
            client.close()
        if cont.status()[0] != "gone":
            # [olr] stop_signal = "TERM": end like a container stop (docker stop / Kubernetes), default INT
            sig = str(((spec or {}).get("olr") or {}).get("stop_signal", "INT"))
            run_info["stop_signal"] = sig
            run_info["exit_code"] = cont.stop(sig) if run_info["exit_code"] is None else run_info["exit_code"]
            (workdir / "olr.log").write_text(cont.logs())
            cont.remove()
        out.close()
        rawlog.close()
        for c in sessions.values():
            try:
                c.rollback()
                c.close()
            except Exception:
                pass
        rconn.close()
        run_info["seconds"] = round(time.time() - t0, 1)
        run_info["client_events"] = events
        run_info["requests"] = requests
        (workdir / "run.json").write_text(json.dumps(run_info, indent=1))
    return run_info


class _Abort(Exception):
    pass
