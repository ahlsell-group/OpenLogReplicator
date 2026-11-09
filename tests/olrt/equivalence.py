"""Restart equivalence over recorded redo (network mode, client.toml [equivalence]).

After the live run, the same redo (start SCN to the end of the recording) is replicated again:

  A  one OLR container reads it uninterrupted (state.dormant-transactions-max as in [olr], else 1000).
  S  the restart point: the newest checkpoint of the live run with dormant-tran (with min-tran if none
     has it, then expect_engaged must be false). The live run's checkpoints are used because a replay
     catches up in seconds and writes almost none while transactions are open.
  B  a fresh container on a copy of the live run's state without the checkpoints above S and without
     the writer checkpoint, option on, client START at S
  C  the same with dormant-transactions-max 0 (reads min-tran of the same checkpoint: a classic
     restart)

Checks (failures go to run.json "assert_failures", so check run fails):
  - B and C deliver exactly the same messages (bytes): the restart past dormant transactions changes
    nothing a client sees, begin SCN/time, attributes and c_idx included;
  - B delivers every message of A with commit SCN at or after S, in the same order and equal apart
    from the "scn" field (a restarted run raises the SCN of messages below its start SCN to it) and the
    schema column list (printed once per table and process with schema "first");
  - B's log shows the dormant position was used (unless expect_engaged = false), C's does not;
  - no 60061, 50039, 70003, 70004 in B or C, and the same 60011 lines in both;
  - the same transactions committed below the stream position (60048: xid, commit and position SCN) in B and C;
  - every checkpoint SCN both B and C wrote a <db>-position-<scn>.json for has the same position SCN.

client.toml:

  [equivalence]
  expect_engaged = true
"""
import json
import re
import shutil
import subprocess
import time

from . import netclient

ENGAGED = re.compile(r"restart past dormant transactions: from seq")


def _read(client, sink, idle):
    last = None
    deadline = time.time() + 600
    while time.time() < deadline:
        try:
            got = client.read_event(timeout=idle)
        except (netclient.ConnectionLost, OSError):
            return last
        if got is None:
            return last
        raw, msg, _ = got
        if raw is None:
            continue
        sink.append(raw)
        if "c_scn" in msg:
            last = (int(msg["c_scn"]), int(msg["c_idx"]) if "c_idx" in msg else None)
    return last


def _run(image, scenario, profile, workdir, olr_opts, start_scn, make_config, container_cls, free_port, version, state_from=None,
         keep_upto=None):
    if workdir.exists():
        shutil.rmtree(workdir)
    (workdir / "state").mkdir(parents=True)
    if state_from is not None:
        # only the metadata checkpoints up to keep_upto (complete files: the live OLR may still be writing newer ones)
        for p in state_from.glob(f"{scenario.db_name}-chkpt-*.json"):
            stem = p.stem.rsplit("-", 1)[1]
            if stem.isdigit() and int(stem) <= keep_upto:
                shutil.copy2(p, workdir / "state" / p.name)
    workdir.chmod(0o777)
    (workdir / "state").chmod(0o777)
    for p in (workdir / "state").iterdir():
        p.chmod(0o666)
    (workdir / "config.json").write_text(json.dumps(make_config(scenario, profile, olr_opts, version=version, image=image), indent=2))
    port = free_port()
    cont = container_cls(image, workdir, port)
    out = []
    logs = ""
    try:
        cont.start()
        time.sleep(2)
        client = netclient.Client(scenario.db_name)
        client.connect("127.0.0.1", port, start_scn, None)
        _read(client, out, idle=15)
        client.close()
        cont.stop("INT")
        r = subprocess.run(["docker", "logs", cont.name], capture_output=True, text=True)
        logs = r.stdout + r.stderr
    finally:
        cont.remove()
    (workdir / "output.jsonl").write_text("".join(o + "\n" for o in out))
    (workdir / "olr.log").write_text(logs)
    return out, logs


def _key(raw):
    msg = json.loads(raw)
    return msg.get("c_scn"), msg.get("c_idx"), msg.get("xid")


def _dedupe(msgs):
    seen, out = set(), []
    for raw in msgs:
        k = _key(raw)
        if k in seen:
            continue
        seen.add(k)
        out.append(raw)
    return out


def _no_scn(raw):
    """A message without what a restart legitimately changes: the scn raised to the start SCN, and the column list
    that formats with schema "first" (json profile) print once per table and process."""
    msg = json.loads(raw)
    msg.pop("scn", None)
    for payload in msg.get("payload", []):
        if isinstance(payload.get("schema"), dict):
            payload["schema"].pop("columns", None)
    return json.dumps(msg, sort_keys=True)


def _positions(state, db):
    out = {}
    for p in state.glob(f"{db}-position-*.json"):
        try:
            doc = json.loads(p.read_text())
            out[int(doc["scn"])] = int(doc["position-scn"])
        except (ValueError, KeyError, OSError):
            continue
    return out


BELOW = re.compile(r"60048 .*xid: (\S+), commit scn: (\d+), sent at scn: (\d+)")


def _first_diff(x, y):
    i = next((i for i, (p, q) in enumerate(zip(x, y)) if p != q), min(len(x), len(y)))
    return i, (x[i][:200] if i < len(x) else "-"), (y[i][:200] if i < len(y) else "-")


def run(image, scenario, profile, workdir, spec, rec, make_config, container_cls, free_port, version):
    eq = spec.get("equivalence") or {}
    base = dict(spec.get("olr", {}))
    on = int(base.get("dormant_max", 1000))
    on = on if on > 0 else 1000
    args = (make_config, container_cls, free_port, version)
    a, _ = _run(image, scenario, profile, workdir / "eq-a", dict(base, dormant_max=on), rec["start_scn"], *args)
    state = workdir / "state"
    chk = []
    for p in state.glob(f"{scenario.db_name}-chkpt-*.json"):
        head = p.read_bytes()[:8192].decode(errors="replace")
        chk.append((int(p.stem.rsplit("-", 1)[1]), "dormant-tran" in head, "min-tran" in head))
    chk.sort()
    with_dormant = [c for c in chk if c[1]]
    with_min = [c for c in chk if c[2]]
    if not (with_dormant or with_min):
        return ["equivalence: the live run wrote no checkpoint with an open transaction"], {"checkpoints": chk}
    s = (with_dormant or with_min)[-1][0]
    b, b_log = _run(image, scenario, profile, workdir / "eq-b", dict(base, dormant_max=on), s, *args, state_from=state, keep_upto=s)
    c, c_log = _run(image, scenario, profile, workdir / "eq-c", dict(base, dormant_max=0), s, *args, state_from=state, keep_upto=s)
    fails = []
    b1, c1 = _dedupe(b), _dedupe(c)
    if b1 != c1:
        i, x, y = _first_diff(b1, c1)
        fails.append(f"equivalence: restart past dormant (B, {len(b1)}) differs from min-tran restart (C, {len(c1)}) at {i}: {x} | {y}")
    a_tail = [_no_scn(x) for x in _dedupe(a) if (json.loads(x).get("c_scn") or 0) >= s]
    b_cmp = [_no_scn(x) for x in b1 if (json.loads(x).get("c_scn") or 0) >= s]
    if a_tail != b_cmp:
        i, x, y = _first_diff(a_tail, b_cmp)
        fails.append(f"equivalence: uninterrupted (A, {len(a_tail)} from scn {s}) differs from restarted (B, {len(b_cmp)}) at {i}: {x} | {y}")
    if not a_tail:
        fails.append(f"equivalence: nothing committed after the restart scn {s} to compare")
    if eq.get("expect_engaged", True) and not ENGAGED.search(b_log):
        fails.append(f"equivalence: run B did not restart past dormant transactions (checkpoint {s})")
    if ENGAGED.search(c_log):
        fails.append("equivalence: run C (option off) restarted past dormant transactions")
    # 60011 can be legitimate in both (a transaction that began before the replayed range), but not in one only
    def warns(log, codes):
        return [re.sub(r"^\S+ \S+ ", "", line) for line in log.splitlines() if re.search(rf" (ERROR|WARN)\s+({codes})", line)]
    for name, log in (("B", b_log), ("C", c_log)):
        for line in warns(log, "60061|50039|7000[34]"):
            fails.append(f"equivalence: run {name}: {line[:200]}")
    if warns(b_log, "60011") != warns(c_log, "60011"):
        fails.append(f"equivalence: 60011 differs between B and C: {warns(b_log, '60011')[:3]} | {warns(c_log, '60011')[:3]}")
    below_b = [m.groups() for m in BELOW.finditer(b_log)]
    below_c = [m.groups() for m in BELOW.finditer(c_log)]
    if below_b != below_c:
        fails.append(f"equivalence: 60048 differs between B and C: {below_b[:3]} | {below_c[:3]}")
    pos_b = _positions(workdir / "eq-b" / "state", scenario.db_name)
    pos_c = _positions(workdir / "eq-c" / "state", scenario.db_name)
    common = sorted(set(pos_b) & set(pos_c))
    differ = [(scn, pos_b[scn], pos_c[scn]) for scn in common if pos_b[scn] != pos_c[scn]]
    if differ:
        fails.append(f"equivalence: checkpoint position SCN differs between B and C (scn, B, C): {differ[:3]}")
    info = {"restart_scn": s, "positions_compared": len(common), "below_position": len(below_b), "checkpoints": len(chk), "with_dormant": len(with_dormant), "a": len(a), "a_after": len(a_tail),
            "b": len(b), "c": len(c), "b_engaged": bool(ENGAGED.search(b_log))}
    return fails, info
