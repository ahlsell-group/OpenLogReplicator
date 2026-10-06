"""Phase 2: run an OLR image over exactly the recorded redo, fresh state dir, file writer.

Reader "online" (dictionary read AS OF the start SCN over OCI) and debug.stop-log-switches,
so OLR reads the archived logs of the recording and stops by itself after the last one.
Arch-only mode (flags bit 0) is not used: on a cold start it never finds an archived log
(see README, findings).
"""
import functools
import hashlib
import json
import os
import re
import shutil
import subprocess
import threading
import time

from . import settings as S

# Format profiles. "debezium" is the format the Debezium OLR adapter expects (preset + the
# overrides Debezium 3.6.x needs). "json" is the plain JSON format with all columns.
PROFILES = {
    "debezium": {
        "format": {"type": "debezium", "scn-type": 4, "timestamp-type": 4, "user-type": 0, "redo-thread": 0},
        "flags": 0,
    },
    "json": {
        "format": {"type": "json", "column": 2, "schema": 1, "scn-type": 8 | 4, "timestamp-type": 8 | 4,
                   "xid": 0, "flush-buffer": 0},
        # SHOW_DDL: lets the replay check see TRUNCATE
        "flags": 32,
    },    # Debezium format with SHOW_DDL (TRUNCATE/DDL visible to the consumer)
    "debezium-ddl": {
        "format": {"type": "debezium", "scn-type": 4, "timestamp-type": 4, "user-type": 0, "redo-thread": 0},
        "flags": 32,
    },
    # Debezium format with the DB host's zone; needs an image with the fork's IANA
    # host-timezone fix (upstream 2.0.0 only accepts +HH:MM)
    "debezium-tz": {
        "format": {"type": "debezium", "scn-type": 4, "timestamp-type": 4, "user-type": 0, "redo-thread": 0},
        "flags": 0,
        "reader": {"host-timezone": "{oracle_tz}"},
    },
}
# OLR 1.9.x has no "debezium" format type and no scn-type/timestamp-type bitmasks beyond
# bit 0/1. These are the 1.9 equivalents: "debezium" is the format block Debezium's docs
# give for OLR (1.6-era "scn-all" became "scn-type": 1 in 1.9), "json" mirrors the 2.0 json
# profile (all columns, schema once, SCN and timestamp in every payload).
PROFILES_19 = {
    "debezium": {
        "format": {"type": "json", "column": 2, "db": 3, "interval-dts": 9, "interval-ytm": 4,
                   "message": 2, "rid": 1, "schema": 7, "scn-type": 1, "timestamp-all": 1},
        "flags": 0,
    },
    "json": {
        "format": {"type": "json", "column": 2, "schema": 1, "scn-type": 1, "timestamp-all": 1,
                   "xid": 0, "flush-buffer": 0},
        "flags": 32,
    },
}
ARCH_ONLY = 0  # flags bit 0 (archived logs only) never starts from an empty state dir on upstream 2.0.0, see README "Findings"


def slug(image):
    s = re.sub(r"[^A-Za-z0-9_.-]+", "_", image)
    return s[-60:] if len(s) > 60 else s


@functools.lru_cache(maxsize=None)
def olr_version(image):
    """(major, minor) of the OLR binary in an image, from --version. Defaults to (2, 0)."""
    out = subprocess.run(["docker", "run", "--rm", "--entrypoint", "/opt/OpenLogReplicator/OpenLogReplicator",
                          image, "--version"], capture_output=True, text=True)
    m = re.search(r"OpenLogReplicator v(\d+)\.(\d+)\.(\d+)", out.stdout + out.stderr)
    return (int(m.group(1)), int(m.group(2))) if m else (2, 0)


def make_config(rec, scenario, profile, state_dir="work/state", version=(2, 0)):
    legacy = version < (2, 0)
    table = PROFILES_19 if legacy else PROFILES
    p = table.get(profile) or table[profile.split("-")[0]]
    nlogs = rec["end_seq"] - rec["start_seq"] + 1
    filt = []
    for t in scenario.tables:
        owner, table = t.split(".")
        f = {"owner": owner, "table": table}
        if t in scenario.keys:
            f["key"] = ",".join(scenario.keys[t])
        filt.append(f)
    cfg = {
        "version": "2.0.0",
        "log-level": 3,
        "trace": 0,
        "memory": {"min-mb": 32, "max-mb": 1024},
        "state": {"type": "disk", "path": state_dir, "interval-s": 600},
        "source": [{
            "alias": "S1",
            "name": scenario.db_name,
            "reader": {
                "type": "online", "user": scenario.olr_user, "password": "olr",
                "server": f"//{S.ORACLE_CONTAINER}:1521/{scenario.db_name}",
                "log-archive-format": "o1_mf_%t_%s_%h_.arc",
                "start-scn": rec["start_scn"],
            },
            "format": p["format"],
            "arch": "online",
            "flags": ARCH_ONLY | p["flags"],
            "debug": {"stop-log-switches": nlogs},
            "filter": {"table": filt},
        }],
        "target": [{
            "alias": "FILE", "source": "S1",
            "writer": {"type": "file", "output": "work/output.jsonl", "new-line": 1,
                       "write-buffer-flush-size": 0},
        }],
    }
    src = cfg["source"][0]
    src["flags"] |= int(getattr(scenario, "olr_flags", 0) or 0)
    src["reader"].update(p.get("reader", {}))
    src["reader"].update(getattr(scenario, "olr_reader", {}) or {})
    # "{oracle_tz}" in a reader value: the DB container's time zone (OLRT_ORACLE_TZ)
    for k, v in src["reader"].items():
        if isinstance(v, str):
            src["reader"][k] = v.replace("{oracle_tz}", S.ORACLE_TZ)
    src.update(getattr(scenario, "olr_source", {}) or {})
    cfg["memory"].update(getattr(scenario, "olr_memory", {}) or {})
    if getattr(scenario, "olr_trace", 0):
        cfg["trace"] = scenario.olr_trace
    if legacy:
        # 1.9: config schema "1.9.0"; memory and state live in the source, not top level
        cfg["version"] = f"{version[0]}.{version[1]}.0"
        src = cfg["source"][0]
        src["memory"] = cfg.pop("memory")
        src["state"] = cfg.pop("state")
    return cfg


_GAP_LOCK = threading.Lock()


def _oracle_exec(*cmd):
    return subprocess.run(["docker", "exec", "-u", "oracle", S.ORACLE_CONTAINER, *cmd],
                          capture_output=True, text=True)


SHIM_SRC = S.ROOT / "docker" / "fault-shim" / "pread_fault.c"
SHIM_SO = S.ROOT / "docker" / "fault-shim" / "build" / "pread_fault.so"
SHIM_MODES = ("estale", "eio", "zero")


def fault_shim():
    """Builds the pread fault shim (LD_PRELOAD) with the host compiler; glibc symbols up to 2.38,
    the OLR images run Debian 13 (glibc 2.41)."""
    if not SHIM_SO.exists() or SHIM_SO.stat().st_mtime < SHIM_SRC.stat().st_mtime:
        SHIM_SO.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["cc", "-std=gnu11", "-shared", "-fPIC", "-O2", "-fno-stack-protector",
                        "-U_FORTIFY_SOURCE", "-D_FORTIFY_SOURCE=0", "-o", str(SHIM_SO), str(SHIM_SRC), "-ldl"],
                       check=True)
    return SHIM_SO


def _fault_act(fault, victim, size, workdir):
    """Applies the fault to the archived log. Returns a short description."""
    action = fault["action"]
    if action == "none":
        return "none"
    if action == "rm":
        r = _oracle_exec("rm", "-f", victim)
        return f"rm rc={r.returncode} {r.stderr.strip()}"
    if action == "truncate":
        keep = int(size * float(fault.get("keep", 0.5))) // 512 * 512
        r = _oracle_exec("truncate", "-s", str(keep), victim)
        return f"truncate {size} -> {keep} rc={r.returncode} {r.stderr.strip()}"
    if action in SHIM_MODES:
        (workdir / "fault.ctl").write_text(f"{action} {os.path.basename(victim)}\n")
        return f"pread on {os.path.basename(victim)} -> {action}"
    raise ValueError(f"unknown archive_fault action {action!r}")


def _run_fault(image, rec, scenario, profile, workdir):
    """archive_fault: damage the Nth recorded archived log before OLR starts (when = "before") or as soon
    as OLR logs "processing redo log" for it (when = "open", default). The file is copied aside first and
    restored afterwards, the recording is shared with other images. slow_us makes every read of the victim
    wait first (pread shim), so the reader is still near the start of the file when the fault hits; the
    OLR trace FILE lines (trace 64) show the offsets read before and after."""
    fault = scenario.archive_fault
    arch = rec["archived"][fault["index"]]
    victim = arch["name"]
    backup = victim + ".olrt-orig"
    r = _oracle_exec("cp", "-p", victim, backup)
    if r.returncode != 0:
        raise RuntimeError(f"cannot back up {victim}: {r.stderr}")
    size = int(_oracle_exec("stat", "-c", "%s", victim).stdout.strip())
    info = {"seq": arch["seq"], "file": victim, "size": size, "action": fault["action"],
            "when": fault.get("when", "open")}
    extra = list(getattr(scenario, "docker_args", []) or [])
    if fault["action"] in SHIM_MODES or fault.get("slow_us"):
        extra += ["-v", f"{fault_shim()}:/opt/olrt-shim/pread_fault.so:ro",
                  "-e", "LD_PRELOAD=/opt/olrt-shim/pread_fault.so"]
    if fault.get("slow_us"):
        # every read of the victim waits first: the fault lands while the reader is near the start of the file
        extra += ["-e", f"OLRT_SLOW_US={int(fault['slow_us'])}", "-e", f"OLRT_SLOW_SUFFIX={os.path.basename(victim)}"]
        info["slow_us"] = int(fault["slow_us"])
    stop = threading.Event()

    def watch():
        needle = f"path: {victim}"
        log = workdir / "olr.log"
        t0 = time.time()
        pos, buf = 0, ""
        while not stop.is_set():
            try:
                with open(log, "r", errors="replace") as f:
                    f.seek(pos)
                    chunk = f.read()
                    pos = f.tell()
            except OSError:
                chunk = ""
            buf += chunk
            lines = buf.split("\n")
            buf = lines.pop()
            for line in lines:
                if "processing redo log:" in line and fault.get("cpus") and "throttled" not in info:
                    # slow OLR down once the dictionary is loaded and redo reading starts, so the reader is
                    # still near the start of the victim when the fault hits
                    u = subprocess.run(["docker", "update", "--cpus", str(fault["cpus"]), container_name(workdir)],
                                       capture_output=True, text=True)
                    info["throttled"] = f"--cpus {fault['cpus']} rc={u.returncode} {u.stderr.strip()}"
                if "processing redo log:" in line and needle in line:
                    time.sleep(float(fault.get("delay_s", 0)))
                    info["result"] = _fault_act(fault, victim, size, workdir)
                    info["at_s"] = round(time.time() - t0, 2)
                    info["trigger"] = line.strip()
                    return
            time.sleep(0.02)

    try:
        with _GAP_LOCK:
            watcher = None
            if info["when"] == "before":
                if fault["action"] in SHIM_MODES:
                    raise ValueError("pread faults need when = \"open\"")
                info["result"] = _fault_act(fault, victim, size, workdir)
            else:
                watcher = threading.Thread(target=watch, daemon=True)
            res = _run(image, rec, scenario, profile, workdir, extra_args=extra, on_start=watcher)
            stop.set()
            if watcher:
                watcher.join(timeout=5)
            if "result" not in info:
                info["result"] = "not triggered: OLR never logged 'processing redo log' for the file"
            res["archive_fault"] = info
            (workdir / "run.json").write_text(json.dumps(res, indent=1))
            return res
    finally:
        stop.set()
        _oracle_exec("mv", "-f", backup, victim)


def run(image, rec, scenario, profile, workdir):
    """Returns a dict with exit code, timing, log path and output path. With archive_gap the Nth
    archived log of the recording is hidden for the duration of the run (runs of such scenarios
    are serialised because the files are shared with the other runs)."""
    if getattr(scenario, "archive_fault", None):
        return _run_fault(image, rec, scenario, profile, workdir)
    gap = getattr(scenario, "archive_gap", None)
    if not gap:
        return _run(image, rec, scenario, profile, workdir)
    victim = rec["archived"][gap["index"]]["name"]
    hidden = victim + ".hidden"
    with _GAP_LOCK:
        r = _oracle_exec("mv", victim, hidden)
        if r.returncode != 0:
            raise RuntimeError(f"cannot hide {victim}: {r.stderr}")
        try:
            res = _run(image, rec, scenario, profile, workdir)
            res["archive_gap"] = {"seq": rec["archived"][gap["index"]]["seq"], "file": victim}
            (workdir / "run.json").write_text(json.dumps(res, indent=1))
            return res
        finally:
            _oracle_exec("mv", hidden, victim)  # restore: the recording is shared with other images


def container_name(workdir):
    return "olrt-olr-" + hashlib.sha1(str(workdir).encode()).hexdigest()[:12]


def _run(image, rec, scenario, profile, workdir, extra_args=(), on_start=None):
    if workdir.exists():
        shutil.rmtree(workdir)
    (workdir / "state").mkdir(parents=True)
    os.chmod(workdir, 0o777)
    os.chmod(workdir / "state", 0o777)
    cfg = make_config(rec, scenario, profile, version=olr_version(image))
    (workdir / "config.json").write_text(json.dumps(cfg, indent=2))
    name = container_name(workdir)
    subprocess.run(["docker", "rm", "-f", name], capture_output=True)
    cmd = ["docker", "run", "--name", name, "--network", S.NETWORK,
           "--user", f"{os.getuid()}:{S.ORACLE_GID}",
           "-v", f"{S.VOLUME}:/opt/oracle/oradata:ro",
           "-v", f"{workdir.resolve()}:/opt/OpenLogReplicator/work",
           "--label", "olrt=1", *extra_args,
           "--entrypoint", "/opt/OpenLogReplicator/OpenLogReplicator",
           image, "-f", "work/config.json"]
    t0 = time.time()
    timed_out = False
    with open(workdir / "olr.log", "w") as logf:
        proc = subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT)
        if on_start is not None:
            on_start.start()
        try:
            rc = proc.wait(timeout=getattr(scenario, "olr_timeout", 0) or S.OLR_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            timed_out = True
            subprocess.run(["docker", "kill", name], capture_output=True)
            rc = proc.wait()
    elapsed = time.time() - t0
    subprocess.run(["docker", "rm", "-f", name], capture_output=True)
    res = {"image": image, "profile": profile, "exit_code": rc, "timed_out": timed_out,
           "seconds": round(elapsed, 1), "log": "olr.log", "output": "output.jsonl"}
    (workdir / "run.json").write_text(json.dumps(res, indent=1))
    return res


def image_info(image):
    out = subprocess.run(["docker", "image", "inspect", image, "--format", "{{.Id}}"],
                         capture_output=True, text=True)
    if out.returncode != 0:
        return None
    ver = subprocess.run(["docker", "run", "--rm", "--entrypoint", "/opt/OpenLogReplicator/OpenLogReplicator",
                          image, "--version"], capture_output=True, text=True)
    return {"id": out.stdout.strip(), "version": (ver.stdout + ver.stderr).strip().splitlines()[-1:]}
