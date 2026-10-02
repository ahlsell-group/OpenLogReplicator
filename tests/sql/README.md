# SQL regression tests

End-to-end tests for OpenLogReplicator (OLR) against a real Oracle database. Each scenario is plain SQL
(`setup.sql`, `workload.sql`). The runner executes it on Oracle Free in Docker, lets OLR read the resulting
redo with the file writer, and checks the **replay invariant**:

> snapshot(before) + OLR's events, in output order = snapshot(after)

The snapshots are `SELECT ... AS OF SCN` at the start and end of the workload. For updates and deletes the
before-image must also equal the replayed row. No expected output files are needed; a wrong value, a missing
or phantom row, a lost or reordered transaction all show up as a mismatch. OLR's log must contain no
`ERROR`/`WARN` line, and OLR must stop by itself after the last archived log of the recording.

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
| `number-extreme-exponent` | 1e-130 and 9.99e125 (known failing, marked `known_failing`) |
| `date-edges` | `DATE`/`TIMESTAMP(0..9)` before 1970, years 0001 and 9999, DST wall times |
| `savepoint-rollback` | `ROLLBACK TO SAVEPOINT` (nested) and a fully rolled-back transaction |
| `interleaved-transactions` | three overlapping sessions, commit order differs from begin order |
| `long-txn-log-switches` | one transaction across four archived logs, a short one committing inside |

Results seen so far (`bersler/openlogreplicator:2.0.0`): all pass except `wide-delete-then-update` (50073)
and `number-extreme-exponent` (known failing).

## Adding a scenario

Create `scenarios/<name>/` with:

- `setup.sql`: runs before the start SCN is taken, not captured. Create a dedicated user and the tables
  (each needs a primary key and `ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS`).
- `workload.sql`: captured. Statements end with `;`, PL/SQL blocks with a line holding only `/`.
  Comment directives: `-- @session N` switches to connection N (default 1, opened on demand),
  `-- @switch_logfile` archives the current redo log. A workload that ends uncommitted is committed.
- `scenario.toml`: `description`, `tables` (`OWNER.TABLE`, these are the OLR filter and the replayed
  tables), optional `events = { c = n, u = n, d = n }` (exact event counts) and optional
  `known_failing = "reason"` (reported as XFAIL, does not fail the run; XPASS when it starts passing).
- `expected.md`: what the output should look like and what the scenario guards.

Keep names generic and values synthetic: this directory is meant to be upstreamable.

## Notes

- OLR is run with the `json` format, `column: 2` (all columns), `schema: 1`, online reader with
  `start-scn`, and `debug.stop-log-switches` so it exits by itself. The runner calls `ALTER SYSTEM ARCHIVE
  LOG CURRENT` before the start SCN and after the workload, so the range is whole archived logs.
- The runner waits 5 s after `setup.sql`: `AS OF SCN` snapshots fail with `ORA-01466` if the table was
  created or altered within a few seconds before that SCN.
- Not covered: DDL, LOBs, network writer, restart/checkpoint behaviour, RAC.
