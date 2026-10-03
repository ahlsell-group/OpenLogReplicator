# SQL regression tests

End-to-end tests for OpenLogReplicator (OLR) against a real Oracle database. Each scenario is plain SQL
(`setup.sql`, `workload.sql`). The runner executes it on Oracle Free in Docker, lets OLR read the resulting
redo (file writer, or network writer with a client that behaves like Debezium), and checks the **replay
invariant**:

> snapshot(before) + OLR's events, in output order = snapshot(after)

The snapshots are `SELECT ... AS OF SCN` at the start and end of the workload. For updates and deletes the
before-image must also equal the replayed row. No expected output files are needed; a wrong value, a missing
or phantom row, a lost or reordered transaction all show up as a mismatch. OLR's log must contain no
`ERROR`/`WARN` line, and OLR must stop by itself after the last archived log of the recording. The database is the
oracle: LogMiner is not used, so event-by-event parity with LogMiner is not checked here; a value OLR gets
wrong is caught through the before-image comparison and the after snapshot.

## Requirements

Docker, Python 3.11+, about 5 GB of disk. Only a local container is used.

```bash
cd tests/sql
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt    # pinned oracledb, thin mode
.venv/bin/python run.py up                                             # Oracle Free, ARCHIVELOG, user "olr"
.venv/bin/python run.py run --image bersler/openlogreplicator:2.0.0    # all scenarios
.venv/bin/python run.py run --image my-olr:dev savepoint-rollback      # one scenario
.venv/bin/python run.py down --volume                                  # remove container and data
```

`--image` is any image that has the binary at `/opt/OpenLogReplicator/OpenLogReplicator` (the official
Dockerfile does). To test a local build: `docker build -t my-olr:dev .` with that Dockerfile. `--keep` keeps
`config.json`, `olr.log` and `output.jsonl` of passing scenarios too (failing ones are always kept) in
`work/<image>/<scenario>/`.

Environment: `OLRSQL_PORT` (default 15221), `OLRSQL_INSTANCE` (suffix for container/network/volume names, to
run several copies side by side), `OLRSQL_ORACLE_TZ` (container time zone, default `Europe/Berlin`),
`OLRSQL_OLR_TIMEOUT` (seconds, default 300). Containers are called `olrsql-*`.

## Scenarios

| scenario | what it covers |
|---|---|
| `wide-delete-then-update` | DELETE on a 65-column table (row ends at column 63), then UPDATE of column 64 on a 64-column table: `ERROR 50073` in OLR 2.0.0 |
| `multi-piece-rows` | 300-column rows stored in two row pieces: per-piece updates, NULLs, row migration, sparse insert |
| `number-scale` | `NUMBER(15,3)` zero and negatives, 38-digit values, `FLOAT`, NULL transitions |
| `number-extreme-exponent` | 1e-130 .. 1e-128 and +-9.99e125 (OLR 2.0.0 writes values below 1e-128 as 0) |
| `binary-float-double` | `BINARY_FLOAT`/`BINARY_DOUBLE` whole numbers at the precision limit, subnormals, extremes (OLR 2.0.0 writes 6 significant digits and halves subnormals) |
| `date-edges` | `DATE`/`TIMESTAMP(0..9)` before 1970, years 0001 and 9999, DST wall times |
| `savepoint-rollback` | `ROLLBACK TO SAVEPOINT` (nested) and a fully rolled-back transaction |
| `interleaved-transactions` | three overlapping sessions, commit order differs from begin order |
| `long-txn-log-switches` | one transaction across four archived logs, a short one committing inside |
| `open-txn-at-start` | cold start (no checkpoint) while a transaction that began two logs earlier is open: whole transaction, no `scn` below the start SCN |
| `net-continue-mid-txn` | network client restarted inside a transaction continues from its stored `c_scn`/`c_idx`: nothing skipped or repeated (upstream #325) |
| `net-long-txn-continue` | long transaction open while later ones commit and are confirmed, client restarted before its commit |
| `host-timezone-name` | `host-timezone` as a tz database name; `tm` checked against the UTC time of each change |
| `xid-logminer-format` | `xid: 3` equals `V$TRANSACTION.XID` |
| `archive-gap-exit-code` | an archived log of the range is missing: error and a non-zero exit code |
| `sigterm-exit-code` | `docker stop` (SIGTERM): clean stop, exit code 0 |
| `root-container-cold-start` | cold start in CDB$ROOT, where `SYS.V_$PDBS` has no row (as on a non-CDB) |

Results seen so far (`bersler/openlogreplicator:2.0.0`): all pass except `wide-delete-then-update` (50073),
`number-extreme-exponent` and `binary-float-double`. The fork fixes all three.

## Adding a scenario

Create `scenarios/<name>/` with:

- `setup.sql`: runs before the start SCN is taken, not captured. Create a dedicated user and the tables
  (each needs a primary key and `ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS`).
- `workload.sql`: captured. Statements end with `;`, PL/SQL blocks with a line holding only `/`.
  Comment directives: `-- @session N` switches to connection N (default 1, opened on demand),
  `-- @switch_logfile` archives the current redo log, `-- @sleep S` waits. A workload that ends uncommitted is
  committed. With a `-- @start` line the statements before it run first and are not captured (e.g. a
  transaction left open); the start SCN is taken there and OLR is started while the rest runs. In network mode
  `-- @client connect | read N | commits N | store | confirm | disconnect | drain [S]` drive the client, see
  `Client` in run.py: only stored messages count, like a connector that keeps its offset in Kafka.
- `scenario.toml`: `description`, `tables` (`OWNER.TABLE`, these are the OLR filter and the replayed
  tables), optional `events = { c = n, u = n, d = n }` (exact event counts) and optional
  `known_failing = "reason"` (reported as XFAIL, does not fail the run; XPASS when it starts passing).
  Optional too:
  - `mode = "network"`: OLR uses its network writer and the workload drives a client (`-- @client` below).
  - `[olr_reader]`, `[olr_format]`: merged into the reader and format of the OLR configuration;
    `{oracle_tz}` in a value is the database host's time zone.
  - `checks`: subset of `["run", "replay", "counts"]` (default all).
  - `expect_exit = "zero" | "nonzero"`, `expect_log = ["regex", ...]`: OLR's exit code, and `ERROR`/`WARN` lines
    that must appear (and are then allowed).
  - `archive_gap = { index = n }`: the n-th archived log of the range (0-based) is hidden from OLR.
  - `stop = "TERM"`: OLR runs without `stop-log-switches` and is stopped with `docker stop` once its output is
    complete.
  - `commit_time_column`, `xid_column`: a column the workload sets to the UTC time of the change, or to the
    transaction's `RAWTOHEX(V$TRANSACTION.XID)`; the event's `tm` (within 5 s) or `xid` must match it.
  - `container = "root"` with `olr_user = { user = "...", password = "..." }`: setup, workload and OLR run in
    CDB$ROOT; setup.sql creates the common user OLR logs in with.
- `expected.md`: what the output should look like and what the scenario guards.

Keep names generic and values synthetic: this directory is meant to be upstreamable.

## Notes

- OLR is run with the `json` format, `column: 2` (all columns), `schema: 1`, online reader with
  `start-scn`, and `debug.stop-log-switches` so it exits by itself. The runner calls `ALTER SYSTEM ARCHIVE
  LOG CURRENT` before the start SCN and after the workload, so the range is whole archived logs.
- The runner waits 5 s after `setup.sql`: `AS OF SCN` snapshots fail with `ORA-01466` if the table was
  created or altered within a few seconds before that SCN.
- Network mode: OLR's network writer, the client in run.py (no extra dependency: the few protobuf fields are
  encoded by hand). OLR is stopped with SIGTERM at the end; `WARN 10056 host disconnected` and
  `10015 caught signal: 15` are expected there.
- Not covered: DDL, LOBs, OLR restarts and checkpoints, RAC.
