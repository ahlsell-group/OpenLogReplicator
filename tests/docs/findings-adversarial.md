# Findings from the adversarial scenarios (`scenarios/adv/`)

Evidence comes from the lab's Oracle Free 23.26 (`OLRT_INSTANCE=adv`). Images referenced below: a build
of fork commit b631f01d ("fork b631f01d"), `bersler/openlogreplicator:2.0.0-pb` (upstream v2.0.0 built
with protobuf), `olrt/olr:2.0.0-ahlsell.1`, and `olrt/olr:ahlsell-rc3` (fork 7077c62d, tag
v2.0.0-ahlsell.3). Run output lands under `results/<run>/olr/<image>/<profile>/<scenario>/` (not
committed; reproduce with `make compare`). Ranked by severity for a Debezium 3.6.1 client on the network
writer with large transactions.

| finding | status |
|---|---|
| F1 `c_idx` one ahead, CONTINUE loses a row | fixed in `fix/continue-cidx` (fork 2dfbd467), v2.0.0-ahlsell.3 |
| F2 queue-size deadlock | open; WARN 60040 logged (`fix/queue-full-warning`); operational limit `queue-size` >= largest transaction |
| F3 ERROR 50073 from 64 columns (minimal repro 65/64/63) | fixed in `fix/50073-delete-valuesmax`, v2.0.0-ahlsell.1 |
| F4 `interval-mb: 0` checkpoints every loop | fixed in `fix/checkpoint-interval-zero`, v2.0.0-ahlsell.3 |

## F1. HIGH (fixed in `fix/continue-cidx`): JSON `c_idx` is one ahead of OLR's internal position, so CONTINUE inside a transaction silently drops one row

Affects upstream v2.0.0 and fork builds without 2dfbd467 (fork b631f01d included).

- **Scenarios:** `adv/net-continue-mid-txn` (the client reconnects, OLR keeps running) and
  `adv/net-kill-mid-big-txn` (OLR gets SIGKILL and restarts from its own checkpoint). On fork b631f01d
  with the debezium profile both FAIL diff and replay.
- **Symptom:** the client stores `(c_scn, c_idx)` of the last message it received, as Debezium does,
  and reconnects with `CONTINUE{c_scn, c_idx=k}`. OLR resumes at the message whose JSON `c_idx` is
  k+2. The message with `c_idx` k+1 never arrives. There is no warning and the exit code is 0. It
  is deterministic: k=2000 loses `T.ID=3000` (`c_idx` 2001), k=1234 loses `ID=2234` (`c_idx` 1235),
  and k=700 after a SIGKILL restart loses `ID=1700` (`c_idx` 701). It also happens with no CONFIRM
  sent at all, so the confirm path is not the cause.
  ```
  output.jsonl (fork b631f01d, net-continue-mid-txn)
  line 2002: c_scn 2291937 c_idx 2000 op c ID 2999     <- last message before the disconnect
  -- CONTINUE c_scn 2291937 c_idx 2000; OLR log: "client requested scn: 2291937, idx: 2000"
  line 2003: c_scn 2291937 c_idx 2002 op c ID 3001     <- c_idx 2001 (ID 3000) is never sent
  diff:   txn (7, 30, 596): 3600 rows in LogMiner, 3599 in OLR
  replay: OLRT_ADVN1.T key ('3000',): in the database, missing after replay
  ```
- **Root cause (v2.0.0 and fork b631f01d):** `Builder::builderBegin()`
  (`src/builder/Builder.h:324`) stores `msg->lwnIdx = lwnIdx++`. `BuilderJson::appendHeader()`
  (`src/builder/BuilderJson.h:293-294`) then writes the builder's `lwnIdx`, which is already
  incremented, as `"c_idx"`. Every message therefore carries `c_idx = msg->lwnIdx + 1`: a
  transaction's begin shows `c_idx: 1` although `processBegin` resets `lwnIdx` to 0. The writer
  filters and confirms on `msg->lwnIdx` (`Metadata::isNewData`, `WriterStream::processConfirm`). A
  client that echoes `c_idx` back is one message ahead, so CONTINUE skips one message and CONFIRM
  releases one message the client has not received. `BuilderProtobuf.h:160` has the same pattern.
- **Upstream:** bersler/OpenLogReplicator#325 (open: "Debezium sends CONTINUE(c_scn, c_idx);
  first message after c_idx is skipped"). `fix/commit-scn-stamping` (`commitScn >= firstDataScn`) does
  not cover it: at b631f01d the JSON and protobuf `c_idx` is `lwnIdx + 1` and the loss reproduces.
- **Fix:** fork commit 2dfbd467 ("Send the message index as c_idx", branch `fix/continue-cidx`) makes the
  message's own index 1-based and writes that as `c_idx`. Not upstream. `adv/net-continue-mid-txn` and
  `adv/net-kill-mid-big-txn` pass on builds with it in both profiles and list those images in `fixed_in`;
  they are xfail on fork b631f01d.
- **Debezium side:** the adapter reads `c_idx` from the message header (`StreamingEvent.java:43`,
  `@JsonProperty("c_idx")`) and sends it back unchanged in CONFIRM (`OlrNetworkClient.java:176`,
  `setCIdx(index)`) and CONTINUE (`OlrNetworkClient.java:195`). Those line numbers are from Debezium
  commit 3b3fa638c5 (v3.5.0.CR1-110); in `v3.6.1.Final` the same CONTINUE call is at line 194. olrt's client
  (`olrt/netclient.py`) echoes `c_idx` the same way.
- **Impact:** on affected images one row is lost whenever Debezium continues from the middle of a large
  transaction. That happens on any Debezium task restart (the CONTINUE path) and on any OLR restart
  that keeps the writer checkpoint. Streaming a transaction of a few hundred thousand rows takes
  minutes and Debezium flushes offsets periodically (every 60 s by default), so a stored position is
  often inside a transaction. On 2.0.0-pb the same test loses many more rows (2 225 of 3 600 arrive)
  because of the rewind-within-current-buffer bug, which the fork fixes in c9d7cee4.

## F2. HIGH (open, WARN 60040): transactions longer than `queue-size` deadlock

- **Scenario:** `adv/net-small-queue` (queue-size 200, a 1 000-row transaction, then a 1-row
  transaction). FAIL on fork b631f01d, on 2.0.0-pb and on v2.0.0-ahlsell.3 (xfail).
- **Symptom:** OLR delivers 351 messages and stops: 200, plus the 150 released by the one CONFIRM
  that Debezium's rule lets through, plus 1. The next transaction never arrives either. Client log:
  `offset=(2315387, 300), CONFIRM not sent (Debezium rule)` ... `read: timeout, 0/2 commits`.
- **Why it matters:** begin-SCN `c_scn` stamping looks like a cause of this deadlock, but the fork's
  commit-SCN stamping (`fix/commit-scn-stamping`) does not fix it. All messages of one transaction
  carry the same `c_scn`, and Debezium 3.6.1 confirms only when `c_scn` grows, so a transaction longer
  than `queue-size` can never be confirmed. `queue-size` must stay above the largest transaction (the
  maximum is 1 000 000), or Debezium must confirm on `(c_scn, c_idx)`. The deadlock itself is known
  (`r-queue-deadlock` in scenario-backlog.md); commit-SCN stamping does not help with it.
- **Status:** open. With `fix/queue-full-warning` OLR logs WARN 60040 once the writer is stuck on a full
  queue, so the stall is visible in the log. `adv/net-small-queue` is xfail. The operational limit
  applies: `queue-size` must be at least the largest transaction.

## F3. MEDIUM (2.0.0 only, fixed in `fix/50073-delete-valuesmax`): ERROR 50073 does not need more than 256 columns

- **Scenarios:** `adv/width-boundary-mix`, `adv/random-s3-n400`, and the per-pair matrix
  `adv/boundary-<wide>-<narrow>-<last non-NULL column>`, each a single transaction over two tables

- **Symptom (2.0.0-pb):** `ERROR 50073 ... table: OLRT_ADVP65_64.W64: missmatch in column details:
  64 < 64`. OLR stops, exits 0 and emits nothing after that point. The smallest repro: DELETE a row
  from a 65-column table whose last non-NULL column is 63, then UPDATE column 64 of a 64-column table
  in the same transaction.

  | pair (DELETE width, UPDATE width, last non-NULL column) | 2.0.0-pb | 2.0.0-ahlsell.1 | fork b631f01d |
  |---|---|---|---|
  | 65, 64, 63 | FFF (`64 < 64`) | PPP | PPP |
  | 255, 64, 63 | FFF (`64 < 64`) | PPP | PPP |
  | 257, 65, 63 | FFF (`65 < 65`) | PPP | PPP |
  | 256, 128, 127 | FFF (`128 < 128`) | PPP | PPP |
  | 257, 129, 64 | FFF (`129 < 129`) | PPP | PPP |
  | 257, 256, 255 | FFF (`256 < 256`) | PPP | PPP |
  | 128, 65, 64 | PPP | PPP | PPP |
  | 129, 65, 64 | PPP | PPP | PPP |

- **Meaning:** tables of 64 and 65 columns are enough (pair 65/64/63), so the blast radius is not
  limited to WIDE_278/WIDE_313 (278/313 columns). What decides failure is more than "the next DML reaches a
  higher column": 257/129/64 fails while 128/65/64 and 129/65/64 pass, so the deleted row's
  last-non-NULL position relative to the 64-column word matters. The matrix above is the evidence; no
  general rule is claimed. 65/64/63 is the smallest public repro. The upstream issue/PR text for
  `fix/50073-delete-valuesmax` says "more than 256 columns", which the matrix shows is too narrow. Fork
  b631f01d and 2.0.0-ahlsell.1 pass every pair.

## Confirmed working on fork b631f01d and later fork builds

- **Builder state across widths:** tables of 5, 63, 64, 65, 127, 128, 129, 255, 256, 257 and 313
  columns pass with all-NULL rows, trailing NULLs, high-column-only and no-op updates, PK-only
  deletes, and rows grown until they migrate or chain inside the transaction (`adv/width-boundary-mix`,
  `adv/random-*`).
- **Seeded random mixes:** seeds 1, 2 and 3 (150, 150 and 400 steps, 3 sessions) mix interleaved
  transactions that commit out of begin order with savepoints, ROLLBACK TO SAVEPOINT, full rollbacks,
  multi-row statements and log switches mid-transaction. run, diff and replay pass in both profiles.
  An uncommitted sweep with seeds 10-15 (300 steps, 4 sessions, widths 5/63/65/128/256/313;
  `random_scenario(seed, 300, sessions=4, widths=[...])` in gen.py) also passes.
- **Statement-level rollbacks:** multi-row INSERT, UPDATE and DELETE on 257- and 313-column rows that
  fail with ORA-00001, ORA-02290 and ORA-02292; a statement rollback followed by ROLLBACK TO
  SAVEPOINT; a 1 500-row wide transaction rolled back across a log switch. No phantom rows and no
  WARN 70003 (`adv/stmt-rollback-wide`).
- **NUMBER/DATE edges:** 0, -0, 0.000, +-999999999999.999, 38-digit integers, +-9.99e125, 1e-127,
  1e-128, neighbours of 2^63 and 2^64, and 1/3; DATE 0001-01-01 and 9999-12-31 23:59:59, 1582, DST
  wall times, 1677 and 2262. Also tested at columns 254..257 (`adv/number-date-edges`).
- **Network restarts:** these pass on fork builds with `fix/commit-scn-stamping`; 2.0.0-pb loses the
  long transaction (begin-SCN `c_scn` stamping):
  - an open transaction on a 300-column row across an OLR SIGINT right after BEGIN and a SIGKILL
    mid-transaction, on the CONTINUE path (`adv/net-restart-open-txn-continue`);
  - a SIGKILL right before the COMMIT of a 5 000-row transaction spanning three log switches
    (`adv/net-restart-before-commit`).

  With the writer checkpoint deleted, START at the commit SCN of a partly delivered transaction
  resends it whole on both images (`adv/net-kill-mid-big-txn-start`).

## F4 (fixed in `fix/checkpoint-interval-zero`): `state.interval-mb: 0` makes OLR checkpoint every loop

- **Symptom (`state/checkpoint-every-loop`, fork b631f01d and fork 2dfbd467):** with interval-s 1800,
  interval-mb 0, keep-checkpoints 200, OLR writes 850-870 metadata checkpoints in about 2 minutes
  (about 7/s) while redo flows; the 200 kept files cover 25-29 s. After 90 s of rolled-back transactions
  the client's offset is older than every kept checkpoint.
- **Cause (v2.0.0 and fork builds without the fix):** `Metadata::writeCheckpoint` skips a checkpoint only if
  `(checkpointBytes - lastCheckpointBytes) / 1024 / 1024 < ctx->checkpointIntervalMb` (unsigned); with 0 that is
  never true, so every 100 ms checkpoint-thread loop with a new checkpoint SCN writes a file.
  `deleteOldCheckpoints` then keeps the newest 200 (plus the last one with a schema).
- **Why the data still arrives:** both images fall back to V$TRANSACTION when no checkpoint is at or below the
  start SCN ("oldest active transaction began at scn ...") and start at the oldest open transaction. The
  checkpoint history itself does not cover the restart point.
- **Control:** state/checkpoint-interval-mb-large (interval-mb 1048576, same workload) keeps 2-4 checkpoints,
  one at or below the offset with the open transaction as min-tran; it passes on every image.
- **Meaning:** a config that sets `interval-mb: 0` to get "time trigger only" gets the opposite in 2.0.0; a very
  large interval-mb gives that. Every checkpoint also rewrites the file (up to tens
  of MB when it carries the schema, every 20th).
- **Fix:** `fix/checkpoint-interval-zero` makes `interval-mb: 0` mean "no size trigger" (checkpoint interval 0 = off), so
  only the time trigger applies. `state/checkpoint-every-loop` lists v2.0.0-ahlsell.3 in `fixed_in` and passes there.
