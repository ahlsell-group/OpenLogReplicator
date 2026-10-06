# OLR differential test harness

Differential regression tests for OpenLogReplicator (OLR). Each scenario runs real SQL against a
local Oracle Free container. The harness then runs any OLR image over exactly the redo that SQL
produced and checks the result three ways, without hand-written expected output:

| check | passes when |
|---|---|
| **run** | OLR read the whole range and stopped by itself: no timeout, no unexpected `ERROR`/`WARN`, output present |
| **diff** | OLR's events equal LogMiner's for the same SCN range: same transactions in the same commit order, per row the same op, table and full before/after image, values compared exactly (see "Value semantics") |
| **replay** | applying OLR's events in commit order to a snapshot taken `AS OF` the start SCN gives exactly the snapshot `AS OF` the end SCN |

Replay is the strongest check: it needs neither LogMiner nor expectations and catches silent
corruption (a wrong value, a missing or phantom row) as long as the change reaches the table.

The harness lives next to the code, so an image built from this checkout is the default thing
to test; any other OLR image (upstream releases, other branches) can run side by side over the
same recordings.

## Quick start

Needs Docker, Python 3.11+, ~10 GB disk. Only local containers are used; nothing connects to any
other database. Run everything from `tests/` (or `make -C tests ...`).

```bash
make venv
make db-up                                   # olrt-oracle on 127.0.0.1:15210, first start ~3 min
make image                                   # olrt/olr:local-<sha> from this checkout (~12 min)
make test                                    # all scenarios against olrt/olr:local-<sha>
make image-ref REF=bersler/OpenLogReplicator@2.0.0     # olrt/olr:2.0.0 from GitHub (tag, branch or commit)
make compare IMAGES="olrt/olr:2.0.0 olrt/olr:local-<sha>"
make compare IMAGES="..." SCENARIOS="wide/ types/number-scale"   # ids, groups or tags
make list                                    # all scenarios
```

`docker/olr/Dockerfile` is the upstream image Dockerfile, changed so the build context is the
repository root: `OPENLOGREPLICATOR_VERSION=local` (the default) builds the checkout, anything
else downloads `<repo>@<version>` from GitHub (tag `v<version>` first, then any ref).

Results land in `results/<run-id>/` (`results/latest` points at the newest):
`summary.md` (table + failure details), `results.json`, `junit.xml`, and per image/profile/scenario
`olr/<image>/<profile>/<scenario>/{config.json,olr.log,output.jsonl,checks.json}`. Recordings
(`record/<scenario>/recording.json`: SCN range, archived logs, snapshots, LogMiner reference) are
kept, so another image can be compared against the same redo later:

```bash
.venv/bin/python -m olrt replicate --run <run-id> --image olrt/olr:new
.venv/bin/python -m olrt check --run <run-id> --image olrt/olr:old --image olrt/olr:new
```

`make db-down` removes the Oracle container, `make db-reset` also its volume (`olrt-oradata`).
Everything Docker-side is called `olrt-*` (container `olrt-oracle`, network `olrt-net`, OLR run
containers `olrt-olr-*`, images `olrt/olr:*`).

**Known issues.** A scenario whose failure is understood says so in `scenario.toml`:

```toml
fixed_in = ["olrt/olr:local*", "olrt/olr:2.0.0-ahlsell.3*"]   # top level, before [known_issue]; fnmatch on the image

[known_issue]
run = "reason"             # check or check@profile (connect: check@<Debezium version>) = reason
diff = "reason"
```

A failing check with a known issue shows as `xfail`, a pass as `XPASS`. On an image matching
`fixed_in` the known issue is not applied, so the check must pass; a failure there is a real FAIL.
This is how a fix lands: add the image pattern to `fixed_in` and the scenario turns into a
regression guard. `olrt/olr:local*` is the image built from this checkout, which carries the fork's
fixes; `olrt/olr:2.0.0-ahlsell.3*` is the same code built from the release tag. Images of upstream
releases keep the known issues.

**Expected warnings.** `allowed_warnings` is a list of regexes for log lines that are fine. `expect_warning`
(one regex) is allowed on every image and additionally required, as a check on `run`, on images matching
`expect_warning_images`, e.g. WARN 60042 (direct-path load) in `storage/direct-path-insert`.

**Instances.** `OLRT_INSTANCE=<name>` gives a suite its own Oracle container, network, volume and
Connect/Redpanda containers (`olrt-oracle-<name>`, `olrt-net-<name>`, `olrt-oradata-<name>`), so several
runs can share one Docker engine; set `OLRT_ORACLE_PORT` per instance (Kafka and Schema Registry ports
derive from it). `OLRT_RESULTS=<dir>` moves `results/` (default `./results`). Other knobs:
`OLRT_OLR_PARALLEL` (default 4), `OLRT_OLR_TIMEOUT` (default 300 s), `OLRT_KEEP_TOPICS`,
`OLRT_ORACLE_TZ` (DB container time zone, default `Europe/Berlin`).

## How a run works

1. **record** (once per run, all images share it). Per scenario: build fixtures (once per run),
   run `setup.sql`, switch log, take the start SCN and the "before" snapshot `AS OF` it, run
   `workload.sql`, take the end SCN, switch log, take the "after" snapshot, list the archived
   logs, and mine them with LogMiner (`DICT_FROM_ONLINE_CATALOG`, or a redo dictionary built
   with `DBMS_LOGMNR_D.BUILD` + `DDL_DICT_TRACKING` for DDL scenarios; `COMMITTED_DATA_ONLY`;
   values from `DBMS_LOGMNR.MINE_VALUE`). Rolled-back savepoint work appears in LogMiner as
   compensating rows; the harness cancels those, so the reference is the net effect.
2. **replicate** per image x profile x scenario, 4 in parallel: a fresh state dir, the online
   reader with `start-scn` = start SCN (dictionary read `AS OF` it over OCI), file writer, and
   `debug.stop-log-switches` = number of recorded logs so OLR stops by itself.
3. **check** and report.

**Network mode.** A scenario with a `client.toml` runs OLR live with the network writer and a
Python client (`olrt/netclient.py`) that speaks OLR's protobuf protocol the way Debezium's
`OlrNetworkClient` (3.6.x) does: INFO, START with the stored offset or CONTINUE with `c_scn`/`c_idx`,
CONFIRM only when `c_scn` grows, and messages below the START SCN skipped. Steps interleave SQL,
reads, offset commits, disconnects and OLR restarts (`olrt/olr_net.py` documents the step format).
These scenarios are recorded live per image and checked the same way. Images without protobuf
show them as skipped.

### Profiles

| profile | format | flags | why |
|---|---|---|---|
| `debezium` | `{"type":"debezium","scn-type":4,"timestamp-type":4,"user-type":0,"redo-thread":0}` | 0 | the format the Debezium OLR adapter expects |
| `json` | `{"type":"json","column":2,"schema":1,...}` | 32 (SHOW_DDL) | plain JSON with all columns; DDL messages visible |
| `debezium-ddl` | as `debezium` | 32 | Debezium format with TRUNCATE/ALTER visible |
| `debezium-tz` | as `debezium`, reader `host-timezone` = the DB container zone | 0 | needs an image that accepts tz database names (fork fix/host-timezone-iana) |

For OLR 1.9.x the harness writes a 1.9 config (`"version":"1.9.0"`, memory/state inside the
source, no debezium preset: the JSON block from Debezium's OLR docs instead). See `olrt/olr.py`.

## Scenarios

`scenarios/<group>/<name>/`:

- `scenario.toml`: `description`, `tags`, `tables` (OWNER.TABLE, the OLR filter and the captured
  tables), optional `fixtures`, `keys` (for tables without PK; also passed as filter `key`),
  `force_logging`, `logminer_dict = "redo"`, `allowed_warnings` (regexes), `expect_olr_error`,
  `expect_exit_code`, `expect_nonzero_exit`, `expect_warning` (regex: allowed everywhere, and check
  run fails without it on images matching `expect_warning_images`), `commit_time` (also compare event
  timestamps with the real commit time), `xid_exact` (xid format 3 must equal LogMiner's XID),
  `olr_flags`, `[olr_reader]` (merged into the reader config; `{oracle_tz}` in a value is the DB
  container's zone), `olr_timeout`, `container = "root"` (tables, OLR user `C##OLR` and OLR's
  connection in CDB$ROOT instead of the PDB: there `SYS.V_$PDBS` has no row for the container, the
  same as on a non-CDB), `[known_issue]` and `fixed_in` (see above), `exactly_once` (diff fails on a
  transaction delivered twice, except the one re-sent at a START SCN; default: a note).
- `setup.sql` (not captured), `workload.sql` (captured), `README.md` (what and why).
- SQL files: statements end with `;`, PL/SQL blocks with a `/` line, SQL*Plus commands are
  ignored. Directives: `-- @session N`, `-- @switch_logfile`, `-- @sleep S`,
  `-- @expect_error ORA-NNNNN`.
- `client.toml` makes it a network-mode scenario, `connect.toml` a Connect-mode scenario.

Use generic names and synthetic values in new scenarios.

Groups:

- `types/`: NUMBER scale and exponent range, DATE/TIMESTAMP edges, CHAR padding, `''` vs NULL,
  non-ASCII text, BINARY_FLOAT/DOUBLE.
- `txn/`: savepoints, rollbacks, interleaved and long transactions, log switches inside a
  transaction, SELECT FOR UPDATE, triggers, XA, tables without PK, cold starts with an open
  transaction.
- `ddl/`: ADD/DROP/MODIFY/RENAME COLUMN, extended statistics, MOVE, TRUNCATE, CREATE TABLE inside
  the range, supplemental logging changes.
- `storage/`: chained and multi-piece rows, compression, direct-path loads, partitions, virtual and
  invisible columns.
- `meta/`: commit timestamps and `host-timezone`, xid format, SIGTERM, archived-log-only mode,
  non-CDB (CDB$ROOT) cold start.
- `wide/`: the ERROR 50073 family on two wide tables, `WIDE.WIDE_278` (278 columns, 30 extended
  statistics created before the last 14 columns, moved so dataobj != obj) and `WIDE.WIDE_313`
  (313 columns); fixture and variants generated by `fixtures/wide/gen.py`, synthetic values.
- `adv/`: adversarial: builder state across 63..313-column tables, seeded random DML mixes,
  statement rollbacks, NUMBER/DATE edges, network restarts mid-transaction; generated by
  `fixtures/adv/gen.py` except `adv/net-*`; findings in `docs/findings-adversarial.md`.
- `restart/`: OLR killed or stopped around a log switch and around a checkpoint in the commit LWN,
  then CONTINUE or START (generated by `fixtures/start-log-switch/gen.py`).
- `state/`: OLR state/checkpoint settings in network mode, e.g. `interval-mb: 0`.
- `archive/`: an archived log removed, truncated or failing to read while OLR reads it
  (`[archive_fault]`: `index`, `action` = `rm` | `truncate` | `estale` | `eio` | `zero` | `none`,
  `when` = `open` | `before`, `keep`, `slow_us`). `estale`/`eio`/`zero` and `slow_us` use an
  LD_PRELOAD pread shim (`docker/fault-shim/`, built with the host `cc`) because the lab has no NFS
  server. These scenarios add the **gap** check: OLR's transactions must be the first k of
  LogMiner's, and fewer than all only with an ERROR, a non-zero exit and no "exhausted number of log
  switches". Also: `olr_source`, `olr_memory`, `olr_trace`, `docker_args` (merged into the OLR config
  / `docker run`).
- `ops/`: an archived log missing from the range.
- `connect/`: real Debezium, see Connect mode.

`docs/scenario-backlog.md` lists further candidates with sources.

### Value semantics

- **NUMBER** is compared as an exact decimal. Oracle stores no scale, so `NUMBER(15,3)` holding
  `0.000` is the same value as `0` in the database, in LogMiner and in OLR; OLR's JSON value is `0`
  and the scale is only in the schema (Debezium restores it). Any float rounding or extra digits
  fail the check.
- **CHAR** must keep its blank padding; **VARCHAR2** `''` is NULL and must arrive as `null`.
- **DATE/TIMESTAMP** are compared as wall time with all 9 fraction digits.
- **BINARY_FLOAT/DOUBLE** are compared as IEEE values (single precision for BINARY_FLOAT).
- A column absent from an event is NULL for INSERT/DELETE and unchanged for UPDATE.

## Findings

Discrepancies this suite found in upstream OLR 2.0.0 (and where noted 1.9.0), with the scenario
that shows them and the fork branch that fixes them. The fork release v2.0.0-ahlsell.3 (7077c62d)
carries every fix listed; the scenarios list it in `fixed_in`.

| finding | scenario | status |
|---|---|---|
| ERROR 50073 after a DELETE whose row ends below a 64-column boundary, then DML on a wider column in the same transaction (65/64/63 is the smallest shape; also 278/313 columns) | `wide/*`, `adv/boundary-*` | fixed: `fix/50073-delete-valuesmax` |
| NUMBER values below 1e-128 written as 0 (exponent byte 0x80 taken for zero) | `types/number-tiny`, `types/number-scale` | fixed: `fix/number-tiny` |
| BINARY_FLOAT/DOUBLE written with 6 significant digits, subnormals halved | `types/binary-float-double` | fixed: `fix/binary-float-double` |
| ERROR 50061 on SELECT FOR UPDATE with ROWDEPENDENCIES (each ingredient alone passes) | `txn/sfu-rowdependencies`, `txn/lock-row-trigger` | fixed: `fix/50061-lkr-rowdeps` |
| OLR exits 0 after a fatal error in a worker thread, and ignores SIGTERM as PID 1 | `ops/archive-gap`, `meta/olr-sigterm` | fixed: `fix/exit-code-on-error` |
| A transaction open at a cold start loses its rows (begin not read, or rows stamped below the START SCN and skipped by the client) | `txn/cold-start-mid-transaction`, `txn/open-txn-at-start`, `connect/cold-start-open-txn-*` | fixed: `fix/cold-start-low-watermark`, `fix/start-boundary-scn` |
| Messages stamped with the begin SCN: a long transaction committed after short ones is skipped after CONTINUE (upstream #330) | `txn/long-open-across-client-restart` | fixed: `fix/commit-scn-stamping` |
| `c_idx` one ahead of the internal position: CONTINUE inside a transaction drops one row (upstream #325) | `adv/net-continue-mid-txn`, `adv/net-kill-mid-big-txn` | fixed: `fix/continue-cidx` |
| A cold START on a non-CDB (or in CDB$ROOT) reads an uninitialised pdb id and can skip every transaction | `meta/root-container-cold-start` | fixed: `fix/non-cdb-dbid` |
| `flags: 1` (archived logs only) never starts from an empty state dir | `meta/arch-only-cold-start` | fixed: `fix/arch-only-resetlogs` |
| `state.interval-mb: 0` checkpoints every loop instead of disabling the size trigger | `state/checkpoint-every-loop` | fixed: `fix/checkpoint-interval-zero` |
| A checkpoint inside the commit LWN loses the transaction from min-tran | `restart/chkpt-in-commit-lwn-*` | fixed: `fix/checkpoint-min-tran-commit-lwn` |
| `host-timezone` accepts only `+HH:MM`, so commit timestamps are off by an hour for half the year | `meta/commit-timestamp-host-tz-iana` | fixed: `fix/host-timezone-iana` |
| xid format 3 in host byte order on big-endian databases (not reproducible on the little-endian lab) | `meta/xid-logminer` | fixed: `fix/xid-logminer-byte-order` (unit tests) |
| An archived log that is truncated or shrinks while read gives a silent gap | `archive/truncate-*`, `archive/zero-mid-read` | fixed: `fix/archive-short-read` (ERROR 40012/40013) |
| Direct-path loads are not decoded (LogMiner agrees) | `storage/direct-path-*`, `storage/basic-compression` | open; the fork logs WARN 60042 |
| A transaction larger than the network writer's `queue-size` deadlocks with a client that confirms only when `c_scn` grows | `adv/net-small-queue` | open; the fork logs WARN 60040; keep `queue-size` above the largest transaction |
| CONTINUE before the client's confirmed position | `connect/restart-task-mid-txn`, `connect/worker-kill-mid-txn` | the fork logs WARN 60039 |
| TRUNCATE is invisible with flags 0; with flags 32 `ALTER TABLE ... MOVE` also emits two DDL messages with binary garbage | `ddl/truncate-then-dml`, `ddl/move-mid-stream` | by design / open |
| OLR 1.9.0 drops DML on the wide tables without a warning | `wide/*` | not fixed (1.9) |

Confirmed working on 2.0.0 and the fork: NUMBER(15,3) incl. 0.000/negatives/38 digits, DATE and
TIMESTAMP(0-9) incl. pre-1970 and 9999, CHAR padding, '' vs NULL, UTF-8 text, savepoint rollback
(also on a 313-column row), interleaved transactions, a 60 000-row transaction, a transaction over
four log switches, MERGE/INSERT ALL/multi-row DML, PK updates, tables without PK, ADD COLUMN,
extended stats, MOVE, SET UNUSED, identity/DEFAULT ON NULL and CREATE TABLE inside the range.

## Connect mode (real Debezium)

Scenarios with a `connect.toml` (group `connect/`) run OLR's network writer against the real Debezium
Oracle connector (OLR adapter) in Kafka Connect, with Redpanda (`redpandadata/redpanda:v24.2.7`, broker +
schema registry) and Avro (heartbeat 5 s, `offset.flush.interval.ms` 1000, `cleanup.policy=delete` so
duplicates stay countable). The Debezium version is a parameter; images are built from
`docker/connect/Dockerfile` (ojdbc11 23.9.0.25.07, Avro converter 7.9.0, sha256-pinned).

```bash
make venv && make db-up
.venv/bin/python -m olrt connect --image olrt/olr:local-<sha> --profile debezium,debezium-ddl \
    --dbz 3.6.1.Final,3.7.0.Final connect/            # builds olrt/connect:<version> if missing
.venv/bin/python -m olrt connect-check --run <run-id> # re-run the checks offline
.venv/bin/python -m olrt connect-down                 # remove broker + Connect
```

Checks: **run** (OLR log, task state per step, expected states), **delivery** (per LogMiner transaction
committed after the snapshot SCN: rows lost, duplicated, or with other values; all deliveries together),
**diff** (LogMiner) and **replay** (from the DB AS OF the snapshot SCN; with `snapshot.mode=initial` the
snapshot records are compared too). Evidence per run in
`results/<run>/connect/<image>/<profile>@dbz-<version>/<scenario>/`: `kafka.jsonl` (decoded records),
`schema-history.jsonl`, `connect.log`, `olr.log`, `run.json` (steps, task states, topic schema columns,
offsets). Topics are deleted after each run (Redpanda with 1 GB allows 256 partitions); set
`OLRT_KEEP_TOPICS=1` to keep them.

What the Connect scenarios show: with flags 0 new columns never reach the topic schema, with flags 32
ADD COLUMN does and the garbage `OBJ_` DDL messages on MOVE are ignored; Debezium's default
`skipped.operations` (`t`) drops the TRUNCATE event; a column Debezium knows but OLR does not send arrives
as an empty or zero default; Debezium 3.6.1 CONFIRMs the last read position rather than the
Kafka-acknowledged one, so a task restart in the middle of a transaction can lose rows (3.7.0 fixes it);
a clean OLR shutdown ("Connection lost") fails the task unless `internal.custom.retriable.exception`
makes it retriable.

Adapter swaps on one connector (`connect/swap-*`, `connect/upgrade-*`; step `connector = "adapter"`,
`worker = "upgrade"`, `[olr] autostart`): LogMiner -> OLR keeps every row (OLR gets START at LogMiner's
offset scn, the oldest open transaction, and re-sends the transactions committed after it: duplicates,
no loss); OLR -> LogMiner as is loses the rows a transaction wrote before the swap (LogMiner mines from
the OLR offset scn, the last commit SCN); with `rewind = "open_transactions"` the connector is stopped,
its offset set to scn = min(oldest open transaction start, offset scn) - 1 and commit_scn = "<offset
scn>:1:" via PATCH /offsets, and nothing is lost or duplicated. Debezium 3.2.2 needs an Oracle whose
banner starts "Oracle Database" (23ai, `OLRT_ORACLE_IMAGE=gvenzl/oracle-free:23.9-slim` on its own
`OLRT_INSTANCE`); against 26ai it fails with "Failed to resolve Oracle database version".

## CI

`.github/workflows/olr-tests.yml` (repository root) runs on `workflow_dispatch` only, on
`ubuntu-24.04`. Inputs: `images` (space-separated; `checkout` builds the checked-out ref,
`build:<repo>@<ref>` builds a GitHub repository at a tag, branch or commit, anything else is pulled
as a public image; default `checkout`), `scenarios` (ids, groups or tags; empty = all) and `profiles`
(default `debezium,json`). Summary, JUnit and OLR logs are uploaded as the `olr-results` artifact. The
job fails on any unexpected failure; known issues (`xfail`) do not fail it. Connect mode is not run in
CI. Disk on the hosted runner is tight: the workflow removes preinstalled toolchains and prunes the
build cache after each image; split large runs with `scenarios`.

## Limitations

- **Byte order.** The lab is little-endian Linux. Anything endian-specific (e.g. the xid byte order)
  needs a big-endian database host and is covered by unit tests where possible.
- **Oracle version.** Oracle AI Database 26ai Free (23.26.2). Redo format version differs from 19c
  (OLR reports 23.6.0); 19c-only layouts are not exercised.
- **Character set.** The lab DB is AL32UTF8. Single-byte character sets (e.g. WE8ISO8859P1, the
  0x80-0x9F range) and OLR's own 8-bit tables are not exercised.
- **LogMiner is a reference, not ground truth.** Where it cannot decode (online catalog after
  DDL) the diff check notes the row and skips its values; replay still applies. LogMiner and OLR
  agree on not seeing direct-path loads here (marked known issue).
- **Recordings age out.** OLR reads the dictionary `AS OF` the start SCN, so replicating an old
  recording needs that undo. With the PDB's default 31 MB NOGUARANTEE undo, recordings older
  than ~45 min fail with `ERROR 10051 ... ORA-01555`, so `db-up` sets `RETENTION GUARANTEE` with
  `undo_retention` 24 h; past that, record again.
- **Single instance, no RAC/ASM, local filesystem** (no NFS behaviour beyond the fault shim).
- **Network mode emulates Debezium's client**; the Connect mode runs the real connector but against a
  single-broker Redpanda and with Docker DNS (a stopped OLR container gives
  `UnresolvedAddressException`, a service with a stable address would refuse connections instead).
- Network-mode results for OLR 1.9.0 are not validated: the emulated client's skip rule drops the
  begin message there (`commit without begin`).

## Layout

```
olrt/            harness (python -m olrt): record, logminer, olr, olr_net, netclient, connect, checks, report
docker/          oracle-init/ (ARCHIVELOG, supplemental logging, OLR grants), olr/ (image Dockerfile),
                 connect/ (Kafka Connect + Debezium), fault-shim/ (LD_PRELOAD pread faults)
fixtures/        generators + SQL for the wide tables, adv/ and restart/ scenarios
scenarios/       the suite
docs/            scenario backlog, adversarial findings
```
