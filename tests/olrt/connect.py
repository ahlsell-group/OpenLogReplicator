"""Debezium Connect mode: OLR (network writer) -> real Debezium Oracle connector (OLR adapter)
-> Kafka (Redpanda, Avro + schema registry). The topics are then checked against the
recorded before/after snapshots (replay) and LogMiner (diff), plus a delivery check (rows
lost or duplicated per transaction) and the connector task state.

A scenario opts in with connect.toml next to scenario.toml:

  [connector]
  snapshot_mode = "no_data"        # or "initial"
  adapter = "olr"                   # or "logminer": the connector starts on the LogMiner adapter
  check_scn = true                  # check "scn": source.scn per row equals LogMiner's redo-record SCN
  heartbeat_ms = 5000               # Debezium default is 0 (off)
  config = { "skipped.operations" = "none" }   # extra connector properties (optional)
  final_task_state = "RUNNING"      # expected task state at the end (default RUNNING, "" = any);
                                    # this and expect_task may be {"3.6.1.Final" = "FAILED", default = "RUNNING"}

  [olr]
  state_interval_s = 600
  autostart = true                  # false: OLR is not started until an `olr = "start"` step


  [[step]]
  connector = "create"   # PUT the connector, wait for the task to stream (snapshot SCN known)
  [[step]]
  sql = "INSERT ..."     # one statement / PL/SQL block, session = N (default 1)
  [[step]]
  sql_file = "workload.sql"   # a whole script (directives as in record.py)
  [[step]]
  wait = "idle"          # consume until no new record for `idle` s (default 6), `timeout` (90)
  [[step]]
  wait = "rows"          # consume until `min_records` new records (e.g. mid-transaction)
  [[step]]
  connector = "restart_task" | "restart" | "pause" | "resume"
  [[step]]
  worker = "kill"        # SIGKILL the Connect worker and start it again (no clean commit)
                         # restart_task/restart/kill take mid_rows = N: fail "run" as inconclusive
                         # if N records were already read (transaction delivered before the restart)
  [[step]]
  olr = "restart"        # signal = "INT"|"KILL", delete_writer_checkpoint = true (as an init container might)
  [[step]]
  olr = "stop" | "start" # OLR down for a while (signal as above); start takes delete_writer_checkpoint
  [[step]]
  connector = "adapter"  # adapter = "logminer" | "olr": PUT the same connector with the other adapter,
                         # offsets kept. rewind = "open_transactions" (to logminer only): stop the
                         # connector, set the offset to scn = oldest open transaction's start SCN,
                         # commit_scn = "<old scn>:1:" so LogMiner skips what OLR already delivered,
                         # then PUT the config and resume
  [[step]]
  worker = "upgrade"     # version = "3.7.0.Final": replace the Connect worker with another Debezium
                         # version on the same group and storage topics (offsets and config kept)
  [[step]]
  expect_task = "FAILED" # task state after `within` s (default 30); mismatch fails "run"
  [[step]]
  switch_logfile = true
  [[step]]
  sleep = 2

The Debezium version is chosen per run (--dbz), the OLR format/flags by the profile.
"""
import fnmatch
import json
import os
import re
import shutil
import subprocess
import time
import tomllib
import urllib.error
import urllib.request

from . import checks, db, kafka, logminer, olr, olr_net, record
from . import settings as S

CONNECT_URL = f"http://127.0.0.1:{S.CONNECT_PORT}"


# ---------------------------------------------------------------- stack

def _docker(*args, check=True):
    return subprocess.run(["docker", *args], capture_output=True, text=True, check=check)


def _exists(name):
    return _docker("inspect", name, check=False).returncode == 0


def _image_of(name):
    return _docker("inspect", "-f", "{{.Config.Image}}", name, check=False).stdout.strip()


def connect_image(version):
    return S.CONNECT_IMAGE.format(version=version)


def build_image(version):
    """olrt/connect:<version>. A version with an -ahlsell.N suffix starts from the upstream image of the
    version before the suffix and overlays docker/connect/overlay/<version>/ (a patched connector jar) onto
    the Oracle connector's plugin directory, replacing the upstream jar of the same name."""
    tag = connect_image(version)
    if _docker("image", "inspect", tag, check=False).returncode == 0:
        return tag
    base = re.sub(r"-ahlsell\.\d+$", "", version)
    overlay = f"overlay/{version}" if base != version else "overlay/none"
    if base != version and not (S.ROOT / "docker" / "connect" / overlay).is_dir():
        raise FileNotFoundError(f"docker/connect/{overlay}/ with the patched connector jar is missing")
    print(f"[connect] building {tag} from {base} with {overlay}", flush=True)
    subprocess.run(["docker", "build", "-t", tag, "--build-arg", f"DEBEZIUM_VERSION={base}",
                    "--build-arg", f"OVERLAY={overlay}", str(S.ROOT / "docker" / "connect")], check=True)
    return tag


def broker_up():
    if _exists(S.BROKER_CONTAINER):
        _docker("start", S.BROKER_CONTAINER)
    else:
        _docker("run", "-d", "--name", S.BROKER_CONTAINER, "--network", S.NETWORK, "--label", "olrt=1",
                "-p", f"127.0.0.1:{S.KAFKA_PORT}:{S.KAFKA_PORT}", "-p", f"127.0.0.1:{S.SR_PORT}:8081",
                S.BROKER_IMAGE, "redpanda", "start", "--smp=1", "--memory=1G", "--overprovisioned",
                "--node-id=0", "--check=false",
                f"--kafka-addr=internal://0.0.0.0:9092,external://0.0.0.0:{S.KAFKA_PORT}",
                f"--advertise-kafka-addr=internal://{S.BROKER_CONTAINER}:9092,"
                f"external://127.0.0.1:{S.KAFKA_PORT}",
                "--schema-registry-addr=0.0.0.0:8081")
    _wait_http(f"http://127.0.0.1:{S.SR_PORT}/subjects", 120)


def storage_id(version):
    return re.sub(r"[^0-9a-zA-Z]", "", version)


def connect_up(version, storage_version=None):
    """Start (or switch to) the Connect worker for one Debezium version. storage_version names the
    group and storage topics to use (default: the version's own), so an upgraded worker keeps the
    offsets and connector configs of the version it replaces."""
    image = build_image(version)
    v = storage_id(storage_version or version)
    if _exists(S.CONNECT_CONTAINER):
        # reuse only a worker on the same image and the same group/storage topics (label set below)
        label = _docker("inspect", "-f", '{{index .Config.Labels "olrt.storage"}}', S.CONNECT_CONTAINER,
                        check=False).stdout.strip()
        if _image_of(S.CONNECT_CONTAINER) == image and label == v:
            _docker("start", S.CONNECT_CONTAINER)
            _wait_http(f"{CONNECT_URL}/connectors", 180)
            return
        _docker("rm", "-f", S.CONNECT_CONTAINER)
    # worker settings: offsets flushed every second (offset.flush.interval.ms 1000)
    _docker("run", "-d", "--name", S.CONNECT_CONTAINER, "--network", S.NETWORK, "--label", "olrt=1",
            "--label", f"olrt.storage={v}",
            "-p", f"127.0.0.1:{S.CONNECT_PORT}:8083",
            "-e", f"BOOTSTRAP_SERVERS={S.BROKER_CONTAINER}:9092",
            "-e", f"GROUP_ID=olrt-connect-{v}",
            "-e", f"CONFIG_STORAGE_TOPIC=olrt-connect-{v}-configs",
            "-e", f"OFFSET_STORAGE_TOPIC=olrt-connect-{v}-offsets",
            "-e", f"STATUS_STORAGE_TOPIC=olrt-connect-{v}-status",
            "-e", "CONNECT_CONFIG_STORAGE_REPLICATION_FACTOR=1",
            "-e", "CONNECT_OFFSET_STORAGE_REPLICATION_FACTOR=1",
            "-e", "CONNECT_STATUS_STORAGE_REPLICATION_FACTOR=1",
            "-e", "OFFSET_FLUSH_INTERVAL_MS=1000",
            "-e", "KEY_CONVERTER=io.confluent.connect.avro.AvroConverter",
            "-e", "VALUE_CONVERTER=io.confluent.connect.avro.AvroConverter",
            "-e", f"CONNECT_KEY_CONVERTER_SCHEMA_REGISTRY_URL=http://{S.BROKER_CONTAINER}:8081",
            "-e", f"CONNECT_VALUE_CONVERTER_SCHEMA_REGISTRY_URL=http://{S.BROKER_CONTAINER}:8081",
            "-e", "HEAP_OPTS=-Xms256m -Xmx1g",
            *debug_env(),
            image)
    _wait_http(f"{CONNECT_URL}/connectors", 180)


def debug_env():
    """OLRT_CONNECT_DEBUG=1: DEBUG for Debezium's OLR adapter (CONFIRM / CONTINUE positions) and
    io.debezium.connector.common (offsets handed to commitOffset)."""
    if not os.environ.get("OLRT_CONNECT_DEBUG"):
        return []
    return ["-e", "CONNECT_LOG4J_LOGGER_IO_DEBEZIUM_CONNECTOR_ORACLE_OLR=DEBUG",
            "-e", "CONNECT_LOG4J_LOGGER_IO_DEBEZIUM_CONNECTOR_COMMON=DEBUG",
            "-e", "CONNECT_LOG4J_APPENDER_STDOUT_THRESHOLD=DEBUG"]


def down():
    for n in (S.CONNECT_CONTAINER, S.BROKER_CONTAINER):
        _docker("rm", "-f", n, check=False)


def _wait_http(url, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            with urllib.request.urlopen(url, timeout=5):
                return
        except (urllib.error.URLError, ConnectionError, OSError):
            time.sleep(2)
    raise TimeoutError(f"{url} not reachable after {timeout}s")


def ensure_dbz_user(logminer=False):
    with db.root() as c:
        n = c.cursor().execute("SELECT COUNT(*) FROM dba_users WHERE username = 'C##DBZUSER'").fetchone()[0]
        if not n:
            sql = (S.ROOT / "docker" / "connect" / "dbz-user.sql").read_text()
            record.run_script(sql, {1: c}, c, "dbz-user.sql")
        if logminer:
            # GRANT is idempotent; the LogMiner privileges are only needed by the adapter-swap scenarios
            sql = (S.ROOT / "docker" / "connect" / "dbz-user-logminer.sql").read_text()
            record.run_script(sql, {1: c}, c, "dbz-user-logminer.sql")


def wait_connector(name, want, within):
    """Connector (not task) state, e.g. STOPPED after PUT /stop."""
    deadline = time.time() + within
    s = None
    while time.time() < deadline:
        s = task_state(name)
        if s["connector"] == want:
            return s
        time.sleep(1)
    return s


# ---------------------------------------------------------------- Connect REST

def rest(method, path, body=None, ok=(200, 201, 202, 204)):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"{CONNECT_URL}{path}", data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            txt = r.read().decode()
            return r.status, (json.loads(txt) if txt.strip() else None)
    except urllib.error.HTTPError as e:
        txt = e.read().decode()
        try:
            return e.code, json.loads(txt)
        except json.JSONDecodeError:
            return e.code, txt


def task_state(name):
    code, st = rest("GET", f"/connectors/{name}/status")
    if code != 200 or not isinstance(st, dict):
        return {"connector": f"HTTP {code}", "task": None, "trace": None}
    t = (st.get("tasks") or [{}])[0]
    return {"connector": st.get("connector", {}).get("state"), "task": t.get("state"),
            "trace": (t.get("trace") or "")[:4000] or None}


def connector_config(sc, spec, prefix, olr_host, adapter=None):
    """A typical OLR-adapter connector config (Avro, schema history in Kafka), adapted to the lab.
    adapter "logminer": the same connector on the LogMiner adapter, with the settings the production
    LogMiner connector uses (hybrid strategy, IN filter mode, transaction retention)."""
    c = spec.get("connector", {})
    adapter = adapter or c.get("adapter", "olr")
    keys = ";".join(f"{t}:{','.join(cols)}" for t, cols in sc.keys.items())
    if adapter == "logminer":
        source = {
            "database.connection.adapter": "logminer",
            "log.mining.strategy": "hybrid",
            "log.mining.query.filter.mode": "in",
            "log.mining.transaction.retention.ms": "3600000",
        }
    else:
        source = {
            "database.connection.adapter": "olr",
            "openlogreplicator.source": S.PDB,
            "openlogreplicator.host": olr_host,
            "openlogreplicator.port": "5000",
        }
    cfg = {
        "connector.class": "io.debezium.connector.oracle.OracleConnector",
        "tasks.max": "1",
        **source,
        "snapshot.mode": c.get("snapshot_mode", "no_data"),
        "snapshot.locking.mode": "none",
        "snapshot.max.threads": "2",
        "max.batch.size": "10000",
        "max.queue.size": "40000",
        "poll.interval.ms": "500",
        "heartbeat.interval.ms": str(c.get("heartbeat_ms", 5000)),
        "provide.transaction.metadata": "true",
        "database.url": f"jdbc:oracle:thin:@//{S.ORACLE_CONTAINER}:1521/FREE",
        "database.user": S.DBZ_USER,
        "database.password": S.DBZ_PASSWORD,
        "database.dbname": "FREE",
        "database.pdb.name": S.PDB,
        "table.include.list": ",".join(sc.tables),
        "topic.prefix": prefix,
        "key.converter": "io.confluent.connect.avro.AvroConverter",
        "key.converter.schema.registry.url": f"http://{S.BROKER_CONTAINER}:8081",
        "value.converter": "io.confluent.connect.avro.AvroConverter",
        "value.converter.schema.registry.url": f"http://{S.BROKER_CONTAINER}:8081",
        "value.converter.enhanced.avro.schema.support": "true",
        "schema.history.internal.kafka.bootstrap.servers": f"{S.BROKER_CONTAINER}:9092",
        "schema.history.internal.kafka.topic": f"olrt-schema-history.{prefix}",
        "schema.history.internal.store.only.captured.tables.ddl": "true",
        "schema.history.internal.store.only.captured.databases.ddl": "true",
        "schema.history.internal.skip.unparseable.ddl": "true",
        "topic.creation.default.replication.factor": "1",
        "topic.creation.default.partitions": "1",
        # delete, not compact, so duplicates are not compacted away before they are counted
        "topic.creation.default.cleanup.policy": "delete",
        "producer.override.linger.ms": "100",
        "producer.override.compression.type": "lz4",
    }
    if keys:
        cfg["message.key.columns"] = keys
    cfg.update({k: str(v) for k, v in c.get("config", {}).items()})
    return cfg


def connect_logs(since):
    r = _docker("logs", "--since", since, S.CONNECT_CONTAINER, check=False)
    return r.stdout + r.stderr


SNAP_SCN = [re.compile(r"SnapshotResult \[status=\w+, offset=OracleOffsetContext \[scn=(\d+)"),
            re.compile(r"snapshot_scn=(\d+)"), re.compile(r"[Ss]napshot.*?SCN[: =]+(\d+)")]


def snapshot_scn(name, since):
    code, off = rest("GET", f"/connectors/{name}/offsets")
    if code == 200 and isinstance(off, dict):
        for o in off.get("offsets", []):
            v = o.get("offset", {}).get("snapshot_scn")
            if v:
                return int(v), "offsets"
    logs = connect_logs(since)
    for p in SNAP_SCN:
        m = p.search(logs)
        if m:
            return int(m.group(1)), "log"
    return None, None


# ---------------------------------------------------------------- topics -> OLR-like output

def to_output(records, sc):
    """Debezium records -> (OLR-like JSONL lines for olrt.checks, snapshot rows, txn markers).

    Data records are merged across topics by Kafka timestamp (per topic in offset order) and
    split into deliveries: a new delivery starts when txId changes or total_order does not
    increase (Debezium restarts the count on redelivery)."""
    data, snap, markers = [], {}, []
    by_topic = {}
    for r in records:
        v = r.get("value")
        if not isinstance(v, dict):
            continue
        if r["topic"].endswith(".transaction"):
            markers.append({"status": v.get("status"), "id": v.get("id"), "events": v.get("event_count"),
                            "offset": r["offset"]})
            continue
        if "op" not in v:
            continue
        by_topic.setdefault(r["topic"], []).append(r)
    merged = []
    for t, rs in by_topic.items():
        rs.sort(key=lambda r: r["offset"])
        for i, r in enumerate(rs):
            merged.append((r["ts"], i, t, r))
    merged.sort(key=lambda x: (x[0], int(x[3]["value"]["source"].get("scn") or 0), x[2], x[1]))
    lines = []
    cur, last_order = None, None

    def close():
        if cur is not None:
            lines.append(json.dumps({"xid": cur, "payload": [{"op": "commit"}]}))

    for _, _, topic, r in merged:
        v = r["value"]
        src = v["source"]
        table = f"{src.get('schema')}.{src.get('table')}"
        if v["op"] == "r":
            snap.setdefault(table, []).append(v["after"])
            continue
        if v["op"] == "t":
            close()
            cur, last_order = None, None
            lines.append(json.dumps({"xid": src.get("txId"), "payload": [
                {"op": "ddl", "sql": f'truncate table "{src.get("schema")}"."{src.get("table")}"'}]}))
            continue
        tx = src.get("txId")
        order = (v.get("transaction") or {}).get("total_order")
        if tx != cur or (order is not None and last_order is not None and order <= last_order):
            close()
            cur = tx
            lines.append(json.dumps({"xid": tx, "payload": [{"op": "begin"}]}))
        last_order = order
        lines.append(json.dumps({"xid": tx, "scn": src.get("scn"), "payload": [{
            "op": v["op"], "schema": {"owner": src.get("schema"), "table": src.get("table")},
            **({"before": v["before"]} if v.get("before") is not None else {}),
            **({"after": v["after"]} if v.get("after") is not None else {})}]}))
    close()
    return lines, snap, markers


def check_delivery(rec, txns, since_scn):
    """Per LogMiner transaction committed after the stream start: rows missing in or
    duplicated on the topics (multiset of full row images, all deliveries together)."""
    r = checks.Result("delivery")
    by = {}
    for t in txns:
        by.setdefault(t["xid"], []).append(t)
    lost = dup = nwrong = 0
    for t in rec["logminer"]["transactions"]:
        if not t["events"] or (t.get("commit_scn") or 0) <= since_scn:
            continue
        xid = tuple(t["xid"])
        # LogMiner could not decode some row (dictionary mismatch after DDL): count by op/table
        coarse = any(e.get("undecodable") for e in t["events"])
        want = {}
        for e in t["events"]:
            k = repr((e["op"], e["table"]) if coarse else
                     (e["op"], e["table"], *checks.full_images(e, rec, already_canon=True)))
            want[k] = want.get(k, 0) + 1
        got = {}
        for d in by.get(xid, []):
            for e in d["events"]:
                k = repr((e["op"], e["table"]) if coarse else (e["op"], e["table"], *checks.full_images(e, rec)))
                got[k] = got.get(k, 0) + 1
        miss = sum(max(0, n - got.get(k, 0)) for k, n in want.items())
        extra = sum(max(0, n - want.get(k, 0)) for k, n in got.items() if k in want)
        foreign = sum(n for k, n in got.items() if k not in want)
        # a row that arrived with different values is "wrong" (diff has the details), not lost
        wrong = min(miss, foreign)
        miss -= wrong
        ndeliv = len(by.get(xid, []))
        if miss:
            lost += miss
            r.fail(f"txn {xid}: {miss} of {len(t['events'])} rows not on the topics ({ndeliv} deliveries)")
        if extra:
            dup += extra
            r.note(f"txn {xid}: {extra} duplicate rows ({ndeliv} deliveries)")
        if foreign:
            nwrong += foreign
            r.fail(f"txn {xid}: {foreign} rows on the topics differ from LogMiner (see diff)")
    lm_x = {tuple(t["xid"]) for t in rec["logminer"]["transactions"]}
    for xid, ds in by.items():
        if xid not in lm_x:
            r.note(f"txn {xid}: on the topics, not in LogMiner's range ({sum(len(d['events']) for d in ds)} rows)")
    r.note(f"{lost} rows lost, {dup} rows duplicated, {nwrong} rows with other values")
    r.lost = lost
    return r


def check_scn(rec, records, since_scn):
    """source.scn of every row equals the SCN of the row's redo record as LogMiner reports it (the LogMiner
    adapter's semantics), compared as multisets per transaction. Transactions delivered with a different
    row count (duplicates, losses) are left to the delivery check."""
    r = checks.Result("scn")
    raw_map = {t["xid_raw"].upper(): tuple(t["xid"]) for t in rec["logminer"]["transactions"]}
    got = {}
    for rec_ in records:
        v = rec_.get("value")
        if not isinstance(v, dict) or v.get("op") not in ("c", "u", "d"):
            continue
        src = v["source"]
        got.setdefault(checks.parse_xid(src.get("txId"), raw_map), []).append(int(src.get("scn") or 0))
    checked = bad = 0
    for t in rec["logminer"]["transactions"]:
        if not t["events"] or (t.get("commit_scn") or 0) <= since_scn:
            continue
        want = sorted(e["scn"] for e in t["events"])
        have = sorted(got.get(tuple(t["xid"]), []))
        if len(have) != len(want):
            r.note(f"txn {tuple(t['xid'])}: {len(have)} rows on the topics, {len(want)} in LogMiner, scn not compared")
            continue
        checked += 1
        if have != want:
            bad += 1
            r.fail(f"txn {tuple(t['xid'])}: source.scn {sorted(set(have))[:4]} vs LogMiner row SCNs {sorted(set(want))[:4]} "
                   f"({len(want)} rows, {len(set(want))} distinct in LogMiner, {len(set(have))} on the topics)")
    r.note(f"{checked} transactions compared, {bad} with other row SCNs")
    return r


# Match the CONTINUE-gap text, not only the code: some fork builds also log 60039 for a full queue.
GAP_WARN = re.compile(r"^\S+ \S+ WARN\s+60039 (client continues from .*)$", re.M)


def check_loss_warn(sc, workdir, image, lost):
    """OLR with the CONTINUE gap warning: rows lost after a restart <=> warning 60039 in OLR's log.
    Only on images listed in the scenario's warn_on_loss_images (others never print it)."""
    if not image or not any(fnmatch.fnmatch(image, p) for p in sc.warn_on_loss_images):
        return None
    r = checks.Result("loss_warn")
    log = (workdir / "olr.log").read_text(errors="replace") if (workdir / "olr.log").exists() else ""
    warns = GAP_WARN.findall(log)
    for w in warns[:3]:
        r.note(f"WARN 60039 {w[:400]}")
    if lost and not warns:
        r.fail(f"{lost} rows lost but OLR logged no warning 60039 (silent gap)")
    elif not lost and warns:
        r.fail(f"no row lost but OLR logged warning 60039 {len(warns)}x")
    else:
        r.note(f"{lost} rows lost, {len(warns)} warning(s) 60039")
    return r


# ---------------------------------------------------------------- run one scenario

def per_version(v, version):
    """A value or {"3.6.1.Final" = x, default = y}."""
    if isinstance(v, dict):
        return v.get(version, v.get("default", ""))
    return v


def load_spec(scenario):
    p = scenario.path / "connect.toml"
    return tomllib.loads(p.read_text()) if p.exists() else None


def run(image, scenario, profile, version, workdir, token="adhoc"):
    spec = load_spec(scenario)
    steps = spec.get("step", [])
    if workdir.exists():
        shutil.rmtree(workdir)
    (workdir / "state").mkdir(parents=True)
    os.chmod(workdir, 0o777)
    os.chmod(workdir / "state", 0o777)
    cfg = olr_net.make_config(scenario, profile, spec.get("olr", {}), version=olr.olr_version(image))
    (workdir / "config.json").write_text(json.dumps(cfg, indent=2))
    storage_v = version  # the Connect group/topics in use; an upgrade step changes the worker, not these
    if spec.get("connector", {}).get("adapter") == "logminer" or any(
            st.get("connector") == "adapter" and st.get("adapter") == "logminer" for st in steps):
        ensure_dbz_user(logminer=True)

    events, states = [], []
    run_info = {"image": image, "profile": profile, "debezium": version, "mode": "network",
                "connect": True, "exit_code": None, "timed_out": False, "skip": None, "log": "olr.log",
                "output": "output.jsonl", "expect_failures": []}
    stamp = re.sub(r"[^a-z0-9]", "", f"{scenario.safe_id}{version}{profile}".lower())[-30:]
    prefix = f"olrt{int(time.time()) % 10**7}{stamp}"
    name = f"olrt-{prefix}"
    run_info["connector"] = name
    rconn, pconn = db.root(), db.pdb()
    sessions = {1: pconn}
    cont = olr_net.OlrContainer(image, workdir, olr_net.free_port())
    # instance-scoped name: other suites on the engine use olrt-olrnet-* too
    cont.name = f"olrt-olrcon-{S.INSTANCE or 'default'}-{abs(hash(str(workdir))) % 10**10}"
    reader = None
    since = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() - 1))
    t0 = time.time()
    snap_scn = None
    base = None

    def state(label):
        s = task_state(name)
        states.append({"t": round(time.time() - t0, 1), "after": label, **s})
        return s

    def mid_guard(step, i, n):
        # the restart must land while the big transaction is still being delivered
        if "mid_rows" in step and n >= int(step["mid_rows"]):
            run_info["expect_failures"].append(
                f"step {i}: {n} records read before the restart >= {step['mid_rows']}: the transaction "
                f"was already delivered, mid-transaction restart not tested (inconclusive)")

    def wait_task(want, within):
        deadline = time.time() + within
        s = None
        while time.time() < deadline:
            s = task_state(name)
            if s["task"] == want:
                return s
            time.sleep(1)
        return s

    try:
        record.apply_fixtures(scenario, pconn, rconn, token)
        if scenario.sql("setup.sql"):
            record.run_script(scenario.sql("setup.sql"), sessions, rconn, "setup.sql")
        pconn.commit()
        db.set_force_logging(rconn, scenario.force_logging)
        dict_seq = None
        if scenario.logminer_dict == "redo":
            # as record.py: dictionary in the redo so LogMiner tracks DDL inside the range
            db.switch_logfile(rconn)
            dict_seq = db.current_seq(rconn)
            rconn.cursor().execute("BEGIN DBMS_LOGMNR_D.BUILD(OPTIONS => DBMS_LOGMNR_D.STORE_IN_REDO_LOGS); END;")
        db.switch_logfile(rconn)
        start_seq = db.current_seq(rconn)
        start_scn = db.current_scn(rconn)
        tables_before = {t: record.table_info(pconn, t, scenario) for t in scenario.tables}
        before = {t: record.snapshot(pconn, t, tables_before[t], start_scn) for t in scenario.tables}
        if spec.get("olr", {}).get("autostart", True):
            cont.start()
            time.sleep(2)
            if cont.status()[0] != "running":
                run_info["exit_code"] = cont.status()[1]
                raise RuntimeError("OLR did not start:\n" + cont.logs()[-2000:])
        reader = kafka.TopicReader(prefix)

        for i, step in enumerate(steps):
            if "sql" in step or "sql_file" in step:
                if "sql_file" in step:
                    text = scenario.sql(step["sql_file"])
                    label = step["sql_file"]
                else:
                    stmt = step["sql"].strip()
                    if not (stmt.endswith(";") or stmt.endswith("/")):
                        stmt += ";" if not stmt.upper().startswith(("BEGIN", "DECLARE")) else "\n/"
                    sid = int(step.get("session", 1))
                    text = (f"-- @session {sid}\n" if sid != 1 else "") + stmt + "\n"
                    label = f"connect.toml step {i}"
                    for s in [sid]:
                        if s not in sessions:
                            sessions[s] = db.pdb()
                for m in re.findall(r"--\s*@session\s+(\d+)", text):
                    if int(m) not in sessions:
                        sessions[int(m)] = db.pdb()
                record.run_script(text, sessions, rconn, label)
                events.append(f"step {i}: sql {label}: {(step.get('sql') or '')[:80]}")
            elif step.get("connector") == "create":
                cc = connector_config(scenario, spec, prefix, cont.name)
                (workdir / "connector.json").write_text(json.dumps(cc, indent=1))
                code, body = rest("PUT", f"/connectors/{name}/config", cc)
                if code not in (200, 201):
                    raise RuntimeError(f"connector create: HTTP {code} {body}")
                s = wait_task("RUNNING", 60)
                # streaming has started once the snapshot SCN is known
                deadline = time.time() + float(step.get("timeout", 120))
                while time.time() < deadline and snap_scn is None:
                    snap_scn, src = snapshot_scn(name, since)
                    if snap_scn is None:
                        reader.poll(1.0)
                run_info["snapshot_scn"] = snap_scn
                if snap_scn:
                    # replay base, taken now: AS OF across a later DDL (MOVE, ADD COLUMN) fails
                    base = {t: record.snapshot(pconn, t, tables_before[t], snap_scn)
                            for t in scenario.tables}
                events.append(f"step {i}: connector created, task {s and s['task']}, snapshot scn {snap_scn}")
                reader.drain_until_idle(idle=float(step.get("idle", 4)), timeout=60)
            elif step.get("wait") == "rows":
                deadline = time.time() + float(step.get("timeout", 60))
                got = 0
                while time.time() < deadline and got < int(step.get("min_records", 1)):
                    got += reader.poll(0.5)
                events.append(f"step {i}: wait rows -> {got} new records, {len(reader.records)} in total")
            elif "wait" in step:
                got, to = reader.drain_until_idle(idle=float(step.get("idle", 6)),
                                                  timeout=float(step.get("timeout", 90)),
                                                  min_records=int(step.get("min_records", 0)))
                events.append(f"step {i}: wait idle -> {got} records{' (timeout)' if to else ''}")
            elif step.get("connector") in ("restart_task", "restart", "pause", "resume"):
                what = step["connector"]
                path = {"restart_task": f"/connectors/{name}/tasks/0/restart",
                        "restart": f"/connectors/{name}/restart?includeTasks=true&onlyFailed=false",
                        "pause": f"/connectors/{name}/pause", "resume": f"/connectors/{name}/resume"}[what]
                run_info.setdefault("restarts", []).append(
                    {"step": i, "kind": what, "t": time.time(), "records_before": len(reader.records)})
                code, body = rest("PUT" if what in ("pause", "resume") else "POST", path)
                events.append(f"step {i}: connector {what} -> HTTP {code} ({len(reader.records)} records read before)")
                mid_guard(step, i, len(reader.records))
                time.sleep(float(step.get("settle", 3)))
            elif step.get("worker") == "kill":
                reader.poll(0.2)
                n_before = len(reader.records)
                run_info.setdefault("restarts", []).append(
                    {"step": i, "kind": "worker_kill", "t": time.time(), "records_before": n_before})
                _docker("kill", "-s", "KILL", S.CONNECT_CONTAINER, check=False)
                time.sleep(1)
                _docker("start", S.CONNECT_CONTAINER)
                _wait_http(f"{CONNECT_URL}/connectors", 180)
                events.append(f"step {i}: worker killed ({n_before} records read before) and started")
                mid_guard(step, i, n_before)
                wait_task("RUNNING", 60)
            elif step.get("olr") == "restart":
                cont.stop(step.get("signal", "INT"))
                if step.get("delete_writer_checkpoint"):
                    p = workdir / "state" / f"{S.PDB}-chkpt.json"
                    if p.exists():
                        p.unlink()
                        events.append(f"step {i}: deleted {p.name}")
                subprocess.run(["docker", "start", cont.name], check=True, capture_output=True)
                time.sleep(2)
                events.append(f"step {i}: OLR restarted ({step.get('signal', 'INT')})")
            elif step.get("olr") == "stop":
                rc = cont.stop(step.get("signal", "INT"))
                events.append(f"step {i}: OLR stopped ({step.get('signal', 'INT')}, exit {rc})")
            elif step.get("olr") == "start":
                if step.get("delete_writer_checkpoint"):
                    p = workdir / "state" / f"{S.PDB}-chkpt.json"
                    if p.exists():
                        p.unlink()
                        events.append(f"step {i}: deleted {p.name}")
                if cont.status()[0] == "gone":
                    cont.start()
                else:
                    subprocess.run(["docker", "start", cont.name], check=True, capture_output=True)
                time.sleep(2)
                events.append(f"step {i}: OLR started")
            elif step.get("connector") == "adapter":
                adapter = step["adapter"]
                swap = {"step": i, "adapter": adapter, "t": time.time(), "records_before": len(reader.records)}
                code, off = rest("GET", f"/connectors/{name}/offsets")
                swap["offset_before"] = off
                if step.get("rewind") == "open_transactions":
                    # Before the stop, so a transaction that starts after the query is covered by the
                    # mining start and one that commits before the stop is skipped via commit_scn.
                    s_min = db.scalar(pconn, "SELECT NVL((SELECT MIN(start_scn) FROM v$transaction),"
                                             " (SELECT current_scn FROM v$database)) FROM dual")
                    code, body = rest("PUT", f"/connectors/{name}/stop")
                    st = wait_connector(name, "STOPPED", 60)
                    code, off = rest("GET", f"/connectors/{name}/offsets")
                    old = (off or {}).get("offsets", [{}])[0].get("offset", {})
                    x = int(old["scn"])
                    # offset scn is the exclusive lower bound of the mining range (Debezium itself resumes at
                    # oldest open transaction - 1), and the first change of a transaction sits at its start SCN
                    scn = min(int(s_min), x) - 1
                    # the transaction committed at X that OLR delivered last: named in commit_scn so LogMiner skips
                    # it too (other transactions committed at exactly X would still be re-sent, at-least-once)
                    new = {"scn": str(scn), "commit_scn": f"{x}:1:{old.get('txId') or ''}", "snapshot_scn": str(scn)}
                    code, body = rest("PATCH", f"/connectors/{name}/offsets",
                                      {"offsets": [{"partition": {"server": prefix}, "offset": new}]})
                    swap.update(rewind=dict(oldest_open_scn=int(s_min), offset_scn=x, new=new, http=code,
                                            response=body, connector_state=st and st["connector"]))
                    events.append(f"step {i}: rewind: oldest open tx {s_min}, offset scn {x} -> {new} (HTTP {code})")
                    if code not in (200, 204):
                        raise RuntimeError(f"offset patch: HTTP {code} {body}")
                cc = connector_config(scenario, spec, prefix, cont.name, adapter=adapter)
                (workdir / f"connector-{adapter}-{i}.json").write_text(json.dumps(cc, indent=1))
                code, body = rest("PUT", f"/connectors/{name}/config", cc)
                if code not in (200, 201):
                    raise RuntimeError(f"connector adapter {adapter}: HTTP {code} {body}")
                if step.get("rewind"):
                    rest("PUT", f"/connectors/{name}/resume")
                run_info.setdefault("adapter_swaps", []).append(swap)
                events.append(f"step {i}: connector -> {adapter} (HTTP {code}, {swap['records_before']} records read before)")
                mid_guard(step, i, swap["records_before"])
                time.sleep(float(step.get("settle", 3)))
            elif step.get("worker") == "upgrade":
                new_v = step["version"]
                reader.poll(0.2)
                n_before = len(reader.records)
                connect_up(new_v, storage_version=storage_v)
                run_info.setdefault("upgrades", []).append(
                    {"step": i, "from": version, "to": new_v, "t": time.time(), "records_before": n_before})
                version = new_v
                run_info["debezium_final"] = new_v
                events.append(f"step {i}: worker upgraded to Debezium {new_v} ({n_before} records read before)")
                wait_task("RUNNING", 120)
            elif "expect_task" in step:
                want = per_version(step["expect_task"], version)
                if not want:
                    state(f"step {i}")
                    continue
                s = wait_task(want, float(step.get("within", 30)))
                ok = s and s["task"] == want
                events.append(f"step {i}: expect task {want} -> {s and s['task']}")
                if not ok:
                    run_info["expect_failures"].append(f"step {i}: task {s and s['task']}, expected {want}")
            elif step.get("switch_logfile"):
                db.switch_logfile(rconn)
                events.append(f"step {i}: log switch")
            elif "sleep" in step:
                time.sleep(float(step["sleep"]))
            else:
                raise ValueError(f"connect.toml step {i}: unknown step {step}")
            state(f"step {i}")

        for c in sessions.values():
            c.commit()
        reader.drain_until_idle(idle=6, timeout=90)
        final = state("end")
        want_final = per_version(spec.get("connector", {}).get("final_task_state", "RUNNING"), version)
        run_info["final_task_state_expected"] = want_final
        if want_final and final["task"] != want_final:
            run_info["expect_failures"].append(f"end: task {final['task']}, expected {want_final}")
        end_scn = db.current_scn(rconn)
        db.switch_logfile(rconn)
        end_seq = db.current_seq(rconn) - 1
        tables_after = {t: record.table_info(pconn, t, scenario) for t in scenario.tables}
        after = {t: record.snapshot(pconn, t, tables_after[t], end_scn) for t in scenario.tables}
        archived = db.archived_logs(rconn, start_seq, end_seq)
        lm_logs = archived if dict_seq is None else db.archived_logs(rconn, dict_seq, end_seq)
        lm_tables = {}
        for t in scenario.tables:
            a, b = tables_after.get(t), tables_before.get(t)
            if a and b:
                names = {c["name"] for c in a["columns"]}
                a = dict(a, columns=a["columns"] + [c for c in b["columns"] if c["name"] not in names])
            lm_tables[t] = a or b
        reference = logminer.mine(rconn, lm_logs, start_scn, end_scn, lm_tables,
                                  dictionary=scenario.logminer_dict)
        rec = {
            "id": scenario.id, "mode": "connect",
            "start_scn": start_scn, "end_scn": end_scn, "start_seq": start_seq, "end_seq": end_seq,
            "snapshot_scn": snap_scn,
            "archived": [{"seq": a[0], "name": a[1], "first_scn": a[2], "next_scn": a[3]} for a in archived],
            "db_tz": db.scalar(rconn, "SELECT TO_CHAR(SYSTIMESTAMP, 'TZR') FROM dual"), "host_tz": S.ORACLE_TZ,
            "tables_before": tables_before, "tables": tables_after, "before": before, "after": after,
            "at_snapshot": base, "logminer": reference,
        }
        (workdir / "recording.json").write_text(json.dumps(rec, indent=1, ensure_ascii=False))
    finally:
        if reader is not None:
            reader.poll(1.0)
            with open(workdir / "kafka.jsonl", "w") as f:
                for r in reader.records:
                    f.write(json.dumps(r, ensure_ascii=False, default=str) + "\n")
            # last value schema per table topic: which columns reached the topic schema
            last = {}
            for r in reader.records:
                if r.get("value_schema_id") and not r["topic"].endswith(".transaction"):
                    last[r["topic"]] = r["value_schema_id"]
            run_info["topic_schemas"] = {t: {"schema_id": sid, "columns": list(reader.reg.get(sid)[2])}
                                         for t, sid in last.items()}
            reader.close()
        try:
            states.append({"t": round(time.time() - t0, 1), "after": "final", **task_state(name)})
            code, off = rest("GET", f"/connectors/{name}/offsets")
            run_info["offsets"] = off
        except Exception as e:  # noqa: BLE001 - evidence only
            events.append(f"status at end failed: {e}")
        try:
            hist = kafka.dump_topic(f"olrt-schema-history.{prefix}")
            (workdir / "schema-history.jsonl").write_text("\n".join(h or "" for h in hist) + "\n")
        except Exception as e:  # noqa: BLE001 - evidence only
            events.append(f"schema history dump failed: {e}")
        try:
            # Connect's committed source offsets of this connector, with the time they were written
            offs = kafka.dump_offsets(f"olrt-connect-{storage_id(storage_v)}-offsets", name)
            (workdir / "offsets.jsonl").write_text("".join(json.dumps(o) + "\n" for o in offs))
        except Exception as e:  # noqa: BLE001 - evidence only
            events.append(f"offsets dump failed: {e}")
        (workdir / "connect.log").write_text(connect_logs(since))
        rest("DELETE", f"/connectors/{name}")
        if not os.environ.get("OLRT_KEEP_TOPICS"):
            try:
                run_info["deleted_topics"] = kafka.delete_topics(prefix)
            except Exception as e:  # noqa: BLE001
                events.append(f"topic cleanup failed: {e}")
        if cont.status()[0] != "gone":
            run_info["exit_code"] = cont.stop()
            (workdir / "olr.log").write_text(cont.logs())
            cont.remove()
        for c in sessions.values():
            try:
                c.rollback()
                c.close()
            except Exception:
                pass
        rconn.close()
        run_info["seconds"] = round(time.time() - t0, 1)
        run_info["client_events"] = events
        run_info["task_states"] = states
        (workdir / "run.json").write_text(json.dumps(run_info, indent=1))
    return run_info


# ---------------------------------------------------------------- checks

CHECKS = ["run", "delivery", "loss_warn", "scn", "diff", "replay"]


def apply_known(sc, profile, version, res, image=None):
    """known_issue keys "check@<debezium version>" (e.g. "delivery@3.6.1.Final") first, then
    the usual "check@profile" / "check" via checks.apply_known. None of them on a `fixed_in` image."""
    if checks.fixed_here(sc, image):
        checks.apply_known(sc, profile, res, image)
        return
    rest_ = {}
    for name, r in res.items():
        why = sc.known_issue.get(f"{name}@{version}")
        if why is None:
            rest_[name] = r
        elif r.status == "fail":
            r.status = "xfail"
            r.details.insert(0, f"known issue: {why}")
        elif r.status == "pass":
            r.status = "xpass"
            r.details.insert(0, f"known issue did not reproduce: {why}")
    checks.apply_known(sc, profile, rest_)


def _ev_key(e):
    return (e["op"], e["table"], json.dumps(e.get("before"), sort_keys=True, default=str),
            json.dumps(e.get("after"), sort_keys=True, default=str))


def splice(first, again):
    """A later delivery of the same transaction replaces `first` from the position where its
    first event occurs in `first` (a redelivery from the start or a CONTINUE in the middle);
    if it does not occur there, it is appended (rows in between are then missing, which the
    delivery check reports)."""
    if not again:
        return first
    k0 = _ev_key(again[0])
    for i in range(len(first) - 1, -1, -1):
        if _ev_key(first[i]) == k0:
            return first[:i] + list(again)
    return first + list(again)


def check(sc, workdir):
    run_info = json.loads((workdir / "run.json").read_text())
    rec = json.loads((workdir / "recording.json").read_text())
    records, seen = [], set()
    for l in (workdir / "kafka.jsonl").read_text().splitlines():
        if not l.strip():
            continue
        r_ = json.loads(l)
        pos = (r_.get("topic"), r_.get("partition"), r_.get("offset"))
        if "offset" in r_ and pos in seen:
            continue  # consumer re-read after a rebalance, not a duplicate on the topic
        seen.add(pos)
        records.append(r_)
    lines, snap, markers = to_output(records, sc)
    (workdir / "output.jsonl").write_text("\n".join(lines) + ("\n" if lines else ""))
    out = checks.read_olr(workdir / "output.jsonl", rec)
    res = {"run": checks.check_run(sc, workdir, run_info, out)}
    r = res["run"]
    for e in run_info.get("expect_failures", []):
        r.fail(e)
    failed = [s for s in run_info.get("task_states", []) if s.get("task") == "FAILED"]
    if failed:
        first = (failed[0].get("trace") or "").splitlines()
        r.note(f"task FAILED at {failed[0]['after']}: {first[0][:300] if first else ''}")
    for rec_ in records:
        if "error" in rec_:
            r.fail(f"consumer: {rec_['topic']}: {rec_['error']}")
    log = (workdir / "connect.log").read_text(errors="replace") if (workdir / "connect.log").exists() else ""
    for pat in (r"Failed to connect to OpenLogReplicator", r"Ignoring unparsable DDL statement",
                r"Fetching schema for table", r"Cannot flush latest offset SCN"):
        n = len(re.findall(pat, log))
        if n:
            r.note(f"connect log: {n}x '{pat}'")
    snap_scn = rec.get("snapshot_scn")
    if snap_scn is None:
        r.fail("snapshot SCN unknown: the connector never reached streaming")
        snap_scn = rec["start_scn"]
    # LogMiner reference restricted to what the stream is responsible for
    rec_stream = dict(rec)
    rec_stream["logminer"] = {"transactions": [t for t in rec["logminer"]["transactions"]
                                               if (t.get("commit_scn") or 0) > snap_scn]}
    res["delivery"] = check_delivery(rec, out[0], snap_scn)
    lw = check_loss_warn(sc, workdir, run_info.get("image"), res["delivery"].lost)
    if lw is not None:
        res["loss_warn"] = lw
    if (load_spec(sc) or {}).get("connector", {}).get("check_scn"):
        res["scn"] = check_scn(rec, records, snap_scn)
    # diff/replay see one delivery per transaction: the deliveries spliced in order (a worker
    # kill leaves a partial first delivery and Debezium may continue in the middle of the
    # transaction; duplicates are the delivery check's business)
    best = {}
    for t in out[0]:
        if t["xid"] not in best:
            best[t["xid"]] = dict(t, events=list(t["events"]))
        else:
            best[t["xid"]]["events"] = splice(best[t["xid"]]["events"], t["events"])
    one, seen_x = [], set()
    for t in out[0]:
        if t["xid"] in seen_x:
            continue
        seen_x.add(t["xid"])
        one.append(best.get(t["xid"], t))
    out = (one, out[1], out[2])
    if "diff" in sc.checks:
        res["diff"] = checks.check_diff(sc, rec_stream, out, run_info["profile"])
    if "replay" in sc.checks:
        rec_replay = dict(rec)
        if rec.get("at_snapshot") is not None:
            rec_replay["before"] = rec["at_snapshot"]
        rr = checks.check_replay(sc, rec_replay, out)
        # snapshot.mode=initial: the snapshot records themselves must equal the DB at that SCN
        if snap:
            for t in sc.tables:
                tm = checks.types(rec, t)
                key = (rec["tables"].get(t) or rec["tables_before"][t])["key"]
                want = {checks.key_of(x, key): x for x in (rec.get("at_snapshot") or {}).get(t, [])}
                got = {}
                for row in snap.get(t, []):
                    c = checks.canon_image(row, tm)
                    got[checks.key_of(c, key)] = c
                if want.keys() != got.keys():
                    rr.fail(f"{t}: snapshot records {len(got)} rows, DB at snapshot SCN {len(want)} rows")
                for k in want.keys() & got.keys():
                    if {c: want[k].get(c) for c in want[k]} != {c: got[k].get(c) for c in want[k]}:
                        rr.fail(f"{t} key {k}: snapshot record vs DB: {checks.fmt_row_diff(want[k], got[k])}")
                rr.note(f"{t}: {len(got)} snapshot records")
        res["replay"] = rr
    apply_known(sc, run_info["profile"], run_info["debezium"], res, run_info.get("image"))
    d = {k: v.to_dict() for k, v in res.items()}
    (workdir / "checks.json").write_text(json.dumps(d, indent=1, ensure_ascii=False))
    return d
