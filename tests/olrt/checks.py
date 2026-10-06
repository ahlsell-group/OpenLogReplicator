"""Phase 3: the three checks, each reported on its own.

  run     OLR ran over the whole range: no timeout, no unexpected ERROR/WARN, stopped by
          itself after the last recorded log, produced output.
  diff    OLR's events equal LogMiner's per transaction (op, table, full before/after
          image, exact canonical values), same set of transactions, same commit order.
  replay  applying OLR's events in commit order to the "before" snapshot gives exactly the
          "after" snapshot. Needs no expectations and no LogMiner.
"""
import datetime as dt
import decimal
import fnmatch
import json
import re
import zoneinfo

from . import normalize

MSG = re.compile(r"^\S+ \S+ (ERROR|WARN|FATAL)\s+(\d+)\s*(.*)$")
# Lines that are expected in this lab and say nothing about correctness.
BENIGN_WARN = [
    r"^10003 file: work/state/.*chkpt\.json - get metadata returned: No such file or directory",
]
# network mode: the harness stops OLR with SIGINT and the client disconnects on purpose
BENIGN_NETWORK = [r"^10015 caught signal: 2", r"^10056 host disconnected"]
MAX_DETAILS = 25


class Result:
    def __init__(self, name):
        self.name = name
        self.status = "pass"      # pass | fail | xfail | xpass | skip | error
        self.details = []

    def fail(self, msg):
        self.status = "fail"
        if len(self.details) < MAX_DETAILS:
            self.details.append(msg)
        elif len(self.details) == MAX_DETAILS:
            self.details.append("... (more)")

    def note(self, msg):
        if len(self.details) < MAX_DETAILS:
            self.details.append(msg)

    def to_dict(self):
        return {"status": self.status, "details": self.details}


# ---------------------------------------------------------------- OLR output parsing

def parse_xid(x, raw_map):
    if x is None:
        return None
    s = str(x)
    m = re.match(r"^0x([0-9a-fA-F]+)\.([0-9a-fA-F]+)\.([0-9a-fA-F]+)$", s)
    if m:
        return tuple(int(g, 16) for g in m.groups())
    if re.fullmatch(r"[0-9a-fA-F]{16}", s):
        if s.upper() in raw_map:
            return raw_map[s.upper()]
        b = bytes.fromhex(s)
        return (int.from_bytes(b[0:2], "little"), int.from_bytes(b[2:4], "little"),
                int.from_bytes(b[4:8], "little"))
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)$", s)
    if m:
        return tuple(int(g) for g in m.groups())
    return ("unparsed", s)


def read_olr(path, rec):
    """Returns (transactions, ddl, problems). transactions in output (= commit) order."""
    raw_map = {t["xid_raw"].upper(): tuple(t["xid"]) for t in rec["logminer"]["transactions"]}
    txns, ddl, problems = [], [], []
    open_tx = {}
    if not path.exists():
        return txns, ddl, ["no output file"]
    # errors="replace": an OLR build which mis-decodes a row must show up as a diff failure, not crash the check
    for ln, line in enumerate(path.read_text(errors="replace").splitlines(), start=1):
        if not line.strip():
            continue
        try:
            msg = json.loads(line, parse_float=decimal.Decimal)
        except json.JSONDecodeError as e:
            problems.append(f"line {ln}: invalid JSON: {e}")
            continue
        xid = parse_xid(msg.get("xid"), raw_map)
        for p in msg.get("payload", []):
            op = p.get("op")
            if op == "chkpt":
                continue
            if op == "begin":
                tx = {"xid": xid, "events": [], "tm": msg.get("tm"), "msgs": [msg.get("tm")], "xid_text": msg.get("xid")}
                open_tx[xid] = tx
                continue
            tx = open_tx.get(xid)
            if op == "commit":
                if tx is None:
                    problems.append(f"line {ln}: commit without begin for xid {xid}")
                    continue
                tx["commit_tm"] = msg.get("tm")
                tx["commit_scn"] = msg.get("c_scn")
                txns.append(open_tx.pop(xid))
                continue
            if op == "ddl":
                ddl.append({"xid": xid, "sql": p.get("sql", ""), "pos": len(txns), "msg": p})
                continue
            if op in ("c", "u", "d"):
                if tx is None:
                    tx = open_tx[xid] = {"xid": xid, "events": [], "tm": msg.get("tm"), "msgs": []}
                    problems.append(f"line {ln}: DML without begin for xid {xid}")
                sch = p.get("schema", {})
                tx["events"].append({"op": op, "table": f"{sch.get('owner')}.{sch.get('table')}",
                                     "before": p.get("before"), "after": p.get("after"),
                                     "line": ln})
                tx["msgs"].append(msg.get("tm"))
                continue
            problems.append(f"line {ln}: unknown op {op!r}")
    for xid, tx in open_tx.items():
        problems.append(f"transaction {xid} has no commit in the output ({len(tx['events'])} events)")
    return txns, ddl, problems


# ---------------------------------------------------------------- normalisation

def columns(rec, table):
    info = rec["tables"].get(table) or rec["tables_before"].get(table)
    return info["columns"] if info else []


def types(rec, table):
    t = {c["name"]: c["type"] for c in columns(rec, table)}
    info_b = rec["tables_before"].get(table)
    if info_b:
        for c in info_b["columns"]:
            t.setdefault(c["name"], c["type"])
    return t


def canon_image(img, typemap):
    if img is None:
        return None
    return {k: normalize.canon(v, typemap.get(k, "VARCHAR2")) for k, v in img.items()}


def full_images(ev, rec, already_canon=False):
    """(before_full, after_full) over every known column; absent -> NULL, update after = before + changes."""
    tm = types(rec, ev["table"])
    cols = list(tm)
    b = ev.get("before")
    a = ev.get("after")
    if not already_canon:
        b, a = canon_image(b, tm), canon_image(a, tm)
    bf = af = None
    if ev["op"] in ("u", "d"):
        bf = {c: (b or {}).get(c) for c in cols}
        extra = set(b or {}) - set(cols)
        for c in extra:
            bf[c] = b[c]
    if ev["op"] == "c":
        af = {c: (a or {}).get(c) for c in cols}
    elif ev["op"] == "u":
        af = dict(bf)
        af.update(a or {})
    if af is not None:
        for c in set(a or {}) - set(cols):
            af[c] = a[c]
    return bf, af


def key_of(row, key):
    return tuple(row.get(k) for k in key)


def fmt_row_diff(x, y, limit=6):
    diffs = []
    for c in sorted(set(x or {}) | set(y or {})):
        vx, vy = (x or {}).get(c), (y or {}).get(c)
        if vx != vy:
            diffs.append(f"{c}: {vx!r} != {vy!r}")
    more = f" (+{len(diffs) - limit} more)" if len(diffs) > limit else ""
    return "; ".join(diffs[:limit]) + more


# ---------------------------------------------------------------- check: run

def check_run(sc, workdir, run_info, olr_out, image=None):
    r = Result("run")
    expected_warn = getattr(sc, "expect_warning", None)
    seen_expected_warn = False
    log = (workdir / "olr.log").read_text(errors="replace") if (workdir / "olr.log").exists() else ""
    errors, warns = [], []
    for line in log.splitlines():
        m = MSG.match(line)
        if not m:
            continue
        sev, code, text = m.groups()
        msg = f"{code} {text}"
        if run_info.get("mode") == "network" and any(re.search(p, msg) for p in BENIGN_NETWORK):
            continue
        if sev in ("ERROR", "FATAL"):
            errors.append(msg)
        elif expected_warn and re.search(expected_warn, msg):
            seen_expected_warn = True
            r.note(f"expected WARN {msg[:300]}")
        elif not any(re.search(p, msg) for p in BENIGN_WARN + sc.allowed_warnings):
            warns.append(msg)
    if (expected_warn and image and not seen_expected_warn
            and any(fnmatch.fnmatch(image, p) for p in getattr(sc, "expect_warning_images", []) or [])):
        r.fail(f"expected OLR warning /{expected_warn}/ not seen")
    if sc.archive_fault and sc.archive_fault.get("action") != "none":
        # a damaged archived log: an ERROR and a non-zero exit are fine, the gap check judges the data
        fault = run_info.get("archive_fault", {})
        r.note(f"fault: {fault.get('result')} ({fault.get('when')}, seq {fault.get('seq')}, at {fault.get('at_s')}s)")
        if run_info.get("timed_out"):
            r.fail(f"timed out after {run_info['seconds']}s: OLR neither stopped nor finished "
                   f"({len(errors)} ERROR lines{', last: ' + errors[-1] if errors else ''})")
        if str(fault.get("result", "")).startswith("not triggered"):
            r.fail(fault["result"])
        r.note(f"exit={run_info.get('exit_code')} {run_info.get('seconds')}s, {len(errors)} ERROR lines"
               + (f", first: {errors[0][:160]}" if errors else ""))
        return r
    expected = sc.expect_olr_error
    if expected:
        hit = [e for e in errors if re.search(expected, e)]
        if not hit:
            r.fail(f"expected OLR error /{expected}/ not seen")
        errors = [e for e in errors if not re.search(expected, e)]
    for e in errors:
        r.fail(f"ERROR {e}")
    for w in warns:
        r.fail(f"WARN {w}")
    if run_info.get("timed_out"):
        r.fail(f"timed out after {run_info['seconds']}s (did not stop after the last log switch)")
    network = run_info.get("mode") == "network"
    if not network and "exhausted number of log switches" not in log and not expected:
        r.fail("OLR did not reach the end of the recorded redo (no 'exhausted number of log switches')")
    rc = run_info.get("exit_code")
    if sc.expect_nonzero_exit and rc == 0:
        r.fail("exit status 0 although OLR hit a fatal error (must stop with non-zero exit)")
    elif errors and rc == 0:
        r.note(f"exit status 0 despite ERROR (upstream 2.0.0; fork fix/exit-code-on-error)")
    elif rc not in (0, None) and not run_info.get("timed_out") and not errors and not network and not sc.expect_nonzero_exit:
        r.fail(f"exit status {rc}")
    for f in run_info.get("assert_failures", []):
        r.fail(f)
    if network and run_info.get("read_timeouts"):
        # a client read waited in vain: the stream stalled (e.g. queue-size deadlock)
        r.fail(f"{run_info['read_timeouts']} client read(s) timed out waiting for data")
    if sc.expect_exit_code is not None and rc != sc.expect_exit_code:
        r.fail(f"exit status {rc}, expected {sc.expect_exit_code}"
               + (f" (stopped with SIG{run_info['stop_signal']})" if run_info.get("stop_signal") else ""))
    txns, ddl, problems = olr_out
    for p in problems:
        r.fail(p)
    r.note(f"exit={rc} {run_info.get('seconds')}s, {len(txns)} transactions, {sum(len(t['events']) for t in txns)} rows")
    return r


# ---------------------------------------------------------------- check: gap

def check_gap(sc, rec, olr_out, workdir, run_info):
    """No silent gap: OLR's committed transactions must be the first k of LogMiner's (commit order, same
    row count). Fewer than all is only acceptable when OLR logged an ERROR, exited non-zero and did not
    report that it reached the end of the recorded redo."""
    r = Result("gap")
    txns, _, _ = olr_out
    lm = [t for t in rec["logminer"]["transactions"] if t["events"]]
    olr = [t for t in txns if t["events"]]
    k = 0
    for t in olr:
        if k < len(lm) and t["xid"] == tuple(lm[k]["xid"]) and len(t["events"]) == len(lm[k]["events"]):
            k += 1
            continue
        if k < len(lm) and any(t["xid"] == tuple(x["xid"]) for x in lm[k + 1:]):
            r.fail(f"gap: OLR sent txn {t['xid']} but not LogMiner txn #{k + 1} {tuple(lm[k]['xid'])} "
                   f"({len(lm[k]['events'])} rows) before it")
        else:
            r.fail(f"OLR txn {t['xid']} ({len(t['events'])} rows) does not match LogMiner txn #{k + 1}")
        break
    lm_rows = sum(len(t["events"]) for t in lm)
    olr_rows = sum(len(t["events"]) for t in olr[:k])
    log = (workdir / "olr.log").read_text(errors="replace") if (workdir / "olr.log").exists() else ""
    errors = [m.group(0) for m in (MSG.match(x) for x in log.splitlines()) if m and m.group(1) in ("ERROR", "FATAL")]
    rc = run_info.get("exit_code")
    r.note(f"{k}/{len(lm)} LogMiner transactions delivered in order, {olr_rows}/{lm_rows} rows")
    if k < len(lm) and r.status == "pass":
        missing = len(lm) - k
        reached_end = "exhausted number of log switches" in log
        if reached_end:
            r.fail(f"silent loss: {missing} transactions missing although OLR reported the end of the recorded redo")
        if not errors:
            r.fail(f"silent loss: {missing} transactions missing and no ERROR in the log")
        if rc == 0:
            r.fail(f"{missing} transactions missing and exit status 0")
        if run_info.get("timed_out"):
            r.fail(f"{missing} transactions missing and OLR still running at the timeout")
    return r


# ---------------------------------------------------------------- check: diff

def check_diff(sc, rec, olr_out, profile):
    r = Result("diff")
    txns, ddl, _ = olr_out
    lm = [t for t in rec["logminer"]["transactions"] if t["events"]]
    for t in rec["logminer"]["transactions"]:
        for u in t.get("unsupported", []):
            if u["op"] not in ("DDL",):
                r.note(f"LogMiner {u['op']} in {tuple(t['xid'])}: {u['info']} {u['sql'][:120]}")
    lm_by = {tuple(t["xid"]): t for t in lm}
    olr_by = {}
    for t in txns:
        if t["events"]:
            olr_by.setdefault(t["xid"], t)
    for xid in lm_by.keys() - olr_by.keys():
        r.fail(f"txn {xid}: in LogMiner ({len(lm_by[xid]['events'])} rows), missing in OLR")
    for xid in olr_by.keys() - lm_by.keys():
        r.fail(f"txn {xid}: in OLR ({len(olr_by[xid]['events'])} rows), not in LogMiner")
    order_lm = [tuple(t["xid"]) for t in lm if tuple(t["xid"]) in olr_by]
    order_olr = []
    for t in txns:
        if t["events"] and t["xid"] in lm_by and t["xid"] not in order_olr:
            order_olr.append(t["xid"])
    dups = sum(1 for t in txns if t["events"]) - len({t["xid"] for t in txns if t["events"]})
    if dups and getattr(sc, "exactly_once", False):
        # START(scn) after a restart without writer checkpoint re-sends the transaction committed
        # at exactly that SCN (Debezium 3.6.1 does not drop it): expected, every other repeat fails
        seen, starts = set(), {int(x) for x in rec.get("start_offsets", [])}
        for t in txns:
            if not t["events"]:
                continue
            if t["xid"] in seen:
                if t.get("commit_scn") is not None and int(t["commit_scn"]) in starts:
                    r.note(f"txn {t['xid']}: re-sent once at the START SCN {t['commit_scn']} (boundary, at-least-once)")
                else:
                    r.fail(f"txn {t['xid']}: delivered more than once (exactly_once)")
            seen.add(t["xid"])
    elif dups:
        r.note(f"{dups} transaction(s) delivered more than once (at-least-once redelivery, not loss)")
    if order_lm != order_olr:
        r.fail(f"commit order differs: LogMiner {order_lm} vs OLR {order_olr}")
    for xid in lm_by.keys() & olr_by.keys():
        le = [(e["op"], e["table"], *full_images(e, rec, already_canon=True)) for e in lm_by[xid]["events"]]
        oe = [(e["op"], e["table"], *full_images(e, rec)) for e in olr_by[xid]["events"]]
        if len(le) != len(oe):
            r.fail(f"txn {xid}: {len(le)} rows in LogMiner, {len(oe)} in OLR")
        lm_events = lm_by[xid]["events"]
        for i, (a, b) in enumerate(zip(le, oe)):
            if a == b:
                continue
            if lm_events[i].get("undecodable") and a[:2] == b[:2]:
                r.note(f"txn {xid} row {i}: LogMiner could not decode ({lm_events[i]['undecodable']}), values not compared")
                continue
            if a[:2] != b[:2]:
                r.fail(f"txn {xid} row {i}: LogMiner {a[0]} {a[1]} vs OLR {b[0]} {b[1]}")
                continue
            if a[2] != b[2]:
                r.fail(f"txn {xid} row {i} {a[0]} {a[1]} before: LogMiner vs OLR: {fmt_row_diff(a[2], b[2])}")
            if a[3] != b[3]:
                r.fail(f"txn {xid} row {i} {a[0]} {a[1]} after: LogMiner vs OLR: {fmt_row_diff(a[3], b[3])}")
        if len(le) != len(oe) or le != oe:
            if sorted(map(repr, le)) == sorted(map(repr, oe)):
                r.note(f"txn {xid}: same rows, different order within the transaction")
    # columns that must be present (with the expected value, if given) in every OLR image of a table
    for t, want in sc.assert_columns.items():
        n = 0
        for tx in txns:
            for ev in tx["events"]:
                if ev["table"] != t:
                    continue
                for side in ("before", "after"):
                    img = canon_image(ev.get(side), types(rec, t)) if ev.get(side) is not None else None
                    if img is None:
                        continue
                    n += 1
                    for c in want:
                        if c not in img:
                            r.fail(f"txn {tx['xid']} {ev['op']} {t} {side}: column {c} missing")
        if n == 0:
            r.fail(f"assert_columns: no OLR image for {t}")
        else:
            r.note(f"assert_columns {t} {list(want)}: {n} images checked")
    if sc.commit_time:
        check_commit_time(r, rec, txns, profile)
    if sc.xid_exact:
        check_xid_exact(r, rec, txns)
    return r


def check_xid_exact(r, rec, txns):
    """xid format 3 (the debezium preset) must be V$LOGMNR_CONTENTS.XID byte for byte. parse_xid
    accepts either byte order; here the text itself must be one of LogMiner's raw XIDs. On a
    little-endian DB host both byte orders of 2.0.0 and the fork agree for JSON, so this guards
    the value; the big-endian case (fork fix/xid-logminer-byte-order) needs an AIX/SPARC host."""
    raws = {str(t["xid_raw"]).upper() for t in rec["logminer"]["transactions"] if t["events"]}
    seen = 0
    for t in txns:
        text = t.get("xid_text")
        if not t["events"] or text is None or not re.fullmatch(r"[0-9a-fA-F]{16}", str(text)):
            continue
        seen += 1
        if str(text).upper() not in raws:
            r.fail(f"txn {t['xid']}: xid {text} is not a LogMiner XID (V$LOGMNR_CONTENTS.XID: {sorted(raws)[:3]})")
    if not seen:
        r.note("xid_exact: no transaction with a 16-hex-digit xid (xid format 3) in this profile")
    else:
        r.note(f"xid_exact: {seen} transaction xid(s) equal V$LOGMNR_CONTENTS.XID")


def _tm_to_utc(v, profile):
    if v is None:
        return None
    v = int(v)
    # 2.0 debezium preset: metadata timestamps in ms; json default (and every 1.9 profile): ns.
    # Decide by magnitude so both versions work: ns epochs are > 1e15, ms epochs < 1e14.
    return v / 1e9 if abs(v) > 10**15 else v / 1000


def check_commit_time(r, rec, txns, profile):
    tz = zoneinfo.ZoneInfo(rec["host_tz"])
    lm_by = {tuple(t["xid"]): t for t in rec["logminer"]["transactions"]}
    for t in txns:
        lt = lm_by.get(t["xid"])
        if not lt or not lt.get("commit_time"):
            continue
        local = dt.datetime.fromisoformat(lt["commit_time"]).replace(tzinfo=tz)
        truth = local.timestamp()
        got = _tm_to_utc(t.get("commit_tm") or t.get("tm"), profile)
        if got is None:
            r.fail(f"txn {t['xid']}: no timestamp in OLR output")
            continue
        off = got - truth
        if abs(off) > 2:
            r.fail(f"txn {t['xid']}: OLR tm {dt.datetime.fromtimestamp(got, dt.UTC).isoformat()} vs commit "
                   f"{local.astimezone(dt.UTC).isoformat()} (DB host {rec['host_tz']}): off by {off:+.0f}s")


# ---------------------------------------------------------------- check: replay

def rename_maps(rec, tables):
    """RENAME TABLE / RENAME COLUMN during the workload: ({old table: new table},
    {new table: {old column: new column}}). Tables match by object id (one candidate without it),
    columns by column id (position in recordings without column ids). A column maps only if its old name is gone
    afterwards and its new name did not exist before, so DROP/ADD COLUMN (which renumber) never map."""
    before, after = rec.get("tables_before", {}), rec.get("tables", {})
    tmap = {}
    for t in tables:
        bi = before.get(t)
        if not bi or after.get(t):
            continue
        cands = [u for u in tables if u != t and after.get(u) and not before.get(u)]
        if bi.get("object_id") is not None:
            cands = [u for u in cands if after[u].get("object_id") == bi["object_id"]]
        if len(cands) == 1:
            tmap[t] = cands[0]
    cmap = {}
    for t in tables:
        src = [o for o, n in tmap.items() if n == t]
        bi, ai = before.get(src[0] if src else t), after.get(t)
        if not bi or not ai:
            continue
        bcols, acols = bi["columns"], ai["columns"]
        if all(c.get("column_id") is not None for c in bcols + acols):
            by_id = {c["column_id"]: c["name"] for c in acols}
            pairs = [(c["name"], by_id.get(c["column_id"])) for c in bcols]
        elif len(bcols) == len(acols):
            pairs = [(b["name"], a["name"]) for b, a in zip(bcols, acols)]
        else:
            pairs = []
        bnames, anames = {c["name"] for c in bcols}, {c["name"] for c in acols}
        m = {o: n for o, n in pairs if n and o != n and o not in anames and n not in bnames}
        if m:
            cmap[t] = m
    return tmap, cmap


def _renamed(img, m):
    if not img or not m:
        return img
    return {m.get(k, k): v for k, v in img.items()}


def check_replay(sc, rec, olr_out):
    r = Result("replay")
    txns, ddl, _ = olr_out
    state = {}
    keys = {}
    tmap, cmap = rename_maps(rec, sc.tables)
    for old, new in tmap.items():
        r.note(f"RENAME TABLE {old} -> {new}: its rows and events are replayed as {new}")
    for t, m in cmap.items():
        r.note(f"RENAME COLUMN on {t}: {', '.join(f'{o} -> {n}' for o, n in m.items())}")
    for t in sc.tables:
        info = rec["tables"].get(t) or rec["tables_before"].get(t)
        keys[t] = info["key"] if info else []
        state[t] = {}
    for t in sc.tables:
        dst = tmap.get(t, t)
        for row in rec["before"].get(t, []):
            row = _renamed(dict(row), cmap.get(dst))
            state[dst][key_of(row, keys[dst])] = row
    # DDL that changes data without DML: TRUNCATE (only visible with flags SHOW_DDL)
    ddl_at = {}
    for d in ddl:
        ddl_at.setdefault(d["pos"], []).append(d)

    def apply_ddl(pos):
        for d in ddl_at.get(pos, []):
            m = re.search(r'truncate\s+table\s+"?(\w+)"?\s*\.\s*"?(\w+)"?', d["sql"], re.I)
            if m:
                t = f"{m.group(1).upper()}.{m.group(2).upper()}"
                if t in state:
                    state[t].clear()

    applied = set()
    for n, tx in enumerate(txns):
        apply_ddl(n)
        if tx["events"] and tx["xid"] in applied:
            r.note(f"txn {tx['xid']} delivered again, skipped (consumers dedupe by transaction)")
            continue
        applied.add(tx["xid"])
        for i, ev in enumerate(tx["events"]):
            if ev["table"] in tmap or cmap.get(tmap.get(ev["table"], ev["table"])):
                t = tmap.get(ev["table"], ev["table"])
                ev = dict(ev, table=t, before=_renamed(ev.get("before"), cmap.get(t)),
                          after=_renamed(ev.get("after"), cmap.get(t)))
            t = ev["table"]
            if t not in state:
                r.fail(f"txn {tx['xid']} row {i}: event for table {t} not in the scenario")
                continue
            bf, af = full_images(ev, rec)
            tab = state[t]
            where = f"txn {tx['xid']} row {i} {ev['op']} {t}"
            if ev["op"] == "c":
                k = key_of(af, keys[t])
                if k in tab:
                    r.fail(f"{where}: insert of existing key {k}")
                tab[k] = af
            else:
                k = key_of(bf, keys[t])
                cur = tab.get(k)
                # only columns the event actually carries: an absent column (e.g. one
                # SET UNUSED meanwhile) is not a claim about the row
                present = set(canon_image(ev.get("before"), types(rec, t)) or {})
                if cur is None:
                    r.fail(f"{where}: key {k} not present")
                else:
                    cols = present & set(cur)
                    mism = {c for c in cols if bf[c] != cur.get(c)}
                    if mism:
                        r.fail(f"{where} key {k}: before-image differs from replayed row: "
                               f"{fmt_row_diff({c: cur[c] for c in mism}, {c: bf[c] for c in mism})}")
                if ev["op"] == "d":
                    tab.pop(k, None)
                else:
                    tab.pop(k, None)
                    # nor for UPDATE: a column in neither image keeps its value (with PK-only
                    # supplemental logging the redo has only the key and the changed columns)
                    carried = present | set(canon_image(ev.get("after"), types(rec, t)) or {})
                    merged = dict(cur or {})
                    merged.update({c: v for c, v in af.items() if c in carried})
                    tab[key_of(merged, keys[t])] = merged
    apply_ddl(len(txns))

    for t in sc.tables:
        want = {key_of(row, keys[t]): row for row in rec["after"].get(t, [])}
        got = state[t]
        cols = [c["name"] for c in columns(rec, t)]
        missing = want.keys() - got.keys()
        extra = got.keys() - want.keys()
        for k in sorted(missing, key=repr)[:10]:
            r.fail(f"{t} key {k}: in the database, missing after replay")
        for k in sorted(extra, key=repr)[:10]:
            r.fail(f"{t} key {k}: present after replay, not in the database")
        if len(missing) > 10 or len(extra) > 10:
            r.fail(f"{t}: {len(missing)} missing / {len(extra)} extra rows in total")
        ndiff = 0
        for k in sorted(want.keys() & got.keys(), key=repr):
            a = {c: want[k].get(c) for c in cols}
            b = {c: got[k].get(c) for c in cols}
            if a != b:
                ndiff += 1
                if ndiff <= 10:
                    r.fail(f"{t} key {k}: database vs replay: {fmt_row_diff(a, b)}")
        if ndiff > 10:
            r.fail(f"{t}: {ndiff} rows differ in total")
        r.note(f"{t}: {len(want)} rows in database, {len(got)} after replay")
    return r


def fixed_here(sc, image):
    """True when `image` matches one of the scenario's `fixed_in` patterns (fnmatch on the image
    reference): its known issues are fixed there, so the checks report plain PASS/FAIL."""
    return bool(image) and any(fnmatch.fnmatch(image, p) for p in getattr(sc, "fixed_in", []) or [])


def apply_known(sc, profile, results, image=None):
    """known_issue = {"replay" = "...", "diff@debezium" = "..."} turns fail into xfail, pass into xpass.
    Not applied on images listed in `fixed_in`: there a failure is a regression of the fix."""
    if fixed_here(sc, image):
        for res in results.values():
            if sc.known_issue and res.status in ("pass", "fail"):
                res.note(f"known issue fixed in {image} (fixed_in), not applied")
        return
    for name, res in results.items():
        why = sc.known_issue.get(f"{name}@{profile}") or sc.known_issue.get(name)
        base = profile.split("-")[0]
        if not why and base != profile:
            from .olr import PROFILES
            # a variant inherits its base profile's known issues unless it changes the flags
            if PROFILES.get(profile, {}).get("flags") == PROFILES.get(base, {}).get("flags"):
                why = sc.known_issue.get(f"{name}@{base}")
        if not why:
            continue
        if res.status == "fail":
            res.status = "xfail"
            res.details.insert(0, f"known issue: {why}")
        elif res.status == "pass":
            res.status = "xpass"
            res.details.insert(0, f"known issue did not reproduce: {why}")
