"""Scenario discovery. A scenario is a directory under scenarios/<group>/<name>/ with:

  scenario.toml   metadata (see README.md for the fields)
  setup.sql       optional, runs before the start SCN is taken (not captured)
  workload.sql    runs between start and end SCN (captured)
  README.md       one paragraph: what it tests and why
"""
import dataclasses
import fnmatch
import hashlib
import pathlib
import tomllib

from . import settings as S


@dataclasses.dataclass
class Scenario:
    id: str                      # "<group>/<name>"
    path: pathlib.Path
    description: str
    tables: list[str]            # OWNER.TABLE, OLR filter and captured tables
    tags: list[str] = dataclasses.field(default_factory=list)
    fixtures: list[str] = dataclasses.field(default_factory=list)
    keys: dict = dataclasses.field(default_factory=dict)       # OWNER.TABLE -> [cols] for tables without PK
    force_logging: bool = False
    allowed_warnings: list[str] = dataclasses.field(default_factory=list)  # regexes
    expect_exit_code: int | None = None   # OLR's exit status must be this (also in network mode)
    expect_olr_error: str | None = None   # regex: OLR is expected to stop with this error
    # regex: a WARN that is allowed on every image and required (check run) on images matching expect_warning_images
    expect_warning: str | None = None
    expect_warning_images: list[str] = dataclasses.field(default_factory=list)
    known_issue: dict = dataclasses.field(default_factory=dict)  # check -> text: XFAIL instead of FAIL
    fixed_in: list[str] = dataclasses.field(default_factory=list)  # image patterns where known_issue is fixed
    guards: str = ""
    checks: list[str] = dataclasses.field(default_factory=lambda: ["run", "diff", "replay"])
    exactly_once: bool = False   # diff fails on a transaction delivered twice (default: a note)
    xid_exact: bool = False      # xid format 3 must equal LogMiner's raw XID exactly (check diff)
    commit_time: bool = False    # also compare event timestamps with the DB commit time
    logminer_dict: str = "online"  # "redo": DBMS_LOGMNR_D.BUILD + DDL tracking, for DDL scenarios
    olr_flags: int = 0           # OR'ed into the profile's source.flags
    olr_timeout: int = 0         # seconds; 0 = settings.OLR_TIMEOUT_S
    archive_gap: dict = dataclasses.field(default_factory=dict)  # {"index": N}: rename away the Nth recorded archived log from OLR
    # {"index": N, "action": rm|truncate|estale|eio|zero|none, "when": open|before, "keep": 0.5}: damage the Nth
    # recorded archived log while OLR reads it (see olr._run_fault); judged by the "gap" check
    archive_fault: dict = dataclasses.field(default_factory=dict)
    olr_source: dict = dataclasses.field(default_factory=dict)  # merged into source (arch-read-tries, ...)
    olr_memory: dict = dataclasses.field(default_factory=dict)  # merged into memory (read-buffer-max-mb, ...)
    olr_trace: int = 0           # config "trace" bitmask
    docker_args: list[str] = dataclasses.field(default_factory=list)  # extra `docker run` args for OLR (e.g. --cpus)
    assert_columns: dict = dataclasses.field(default_factory=dict)  # OWNER.TABLE -> columns every OLR image must carry
    expect_nonzero_exit: bool = False  # OLR must exit non-zero (fatal error)
    olr_reader: dict = dataclasses.field(default_factory=dict)  # merged into source.reader
    # "pdb" (default): tables, OLR user and connection in FREEPDB1. "root": everything in CDB$ROOT,
    # where SYS.V_$PDBS has no row for the container, like on a non-CDB
    container: str = "pdb"
    # connect: images with OLR's CONTINUE gap warning (60039): a run that lost rows must log it, a clean run must not
    warn_on_loss_images: list[str] = dataclasses.field(default_factory=list)

    @property
    def mode(self):
        """"connect" with connect.toml (real Debezium Connect, olrt.connect), "network" with
        client.toml (OLR's network writer and the emulated client), else "file"."""
        if (self.path / "connect.toml").exists():
            return "connect"
        return "network" if (self.path / "client.toml").exists() else "file"

    @property
    def db_name(self):
        """Service name OLR connects to and its config "name" (state file prefix, network database name)."""
        return "FREE" if self.container == "root" else S.PDB

    @property
    def con_name(self):
        """SRC_CON_NAME in V$LOGMNR_CONTENTS for the scenario's container."""
        return "CDB$ROOT" if self.container == "root" else S.PDB

    @property
    def olr_user(self):
        return "C##OLR" if self.container == "root" else "olr"

    @property
    def safe_id(self):
        return self.id.replace("/", "__")

    def sql(self, name):
        p = self.path / name
        return p.read_text() if p.exists() else ""


def load(path: pathlib.Path) -> Scenario:
    meta = tomllib.loads((path / "scenario.toml").read_text())
    sid = f"{path.parent.name}/{path.name}"
    tables = [t.upper() for t in meta.pop("tables")]
    keys = {k.upper(): [c.upper() for c in v] for k, v in meta.pop("keys", {}).items()}
    meta["assert_columns"] = {k.upper(): [c.upper() for c in v] for k, v in meta.pop("assert_columns", {}).items()}
    return Scenario(id=sid, path=path, tables=tables, keys=keys, **meta)


def discover(patterns: list[str] | None = None) -> list[Scenario]:
    out = []
    for toml in sorted(S.SCENARIOS.glob("*/*/scenario.toml")):
        sc = load(toml.parent)
        if patterns and not any(fnmatch.fnmatch(sc.id, p) or sc.id.startswith(p.rstrip("/") + "/")
                                or p in sc.tags for p in patterns):
            continue
        out.append(sc)
    return out


def fixture_sql(name: str) -> tuple[str, str]:
    p = S.FIXTURES / name / "setup.sql"
    text = p.read_text()
    return text, hashlib.sha256(text.encode()).hexdigest()[:16]
