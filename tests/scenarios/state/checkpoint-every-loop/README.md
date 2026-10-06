A common setting is `state.interval-mb: 0`, meant as "time trigger only". In 2.0.0
the size check in `Metadata::writeCheckpoint` is `(checkpointBytes - lastCheckpointBytes) / 1024 / 1024 <
checkpointIntervalMb` on unsigned values, so with 0 it is never true and OLR writes a metadata checkpoint on
every checkpoint-thread loop. `keep-checkpoints` then holds the last N loops instead of N x `interval-s`.

The checkpoint thread loops every 100 ms, so while redo flows that is up to 10 checkpoints per second. On
images without the fix the scenario measures 850-870 checkpoint writes in about 2 minutes (all four
image/profile cells), about 7 per second, and the 200 kept files span 25-29 s of wall time instead of
200 x 1800 s = 100 h.

The scenario uses that state block (interval-s 1800, interval-mb 0, keep-checkpoints 200) and
keeps a transaction open across 1 700 short transactions (800 mixed with 80 commits, then a ~90 s
rollback-only tail paced at 0.1 s after the client's last offset), plus a log switch, so OLR writes
hundreds of checkpoints while the client's offset stands still. Before restarting OLR (SIGINT, writer checkpoint deleted first, as an
init container might do) it asserts that a kept checkpoint at or below the client's offset SCN exists
and that its `min-tran` is the open transaction (or an older one already open at that point, e.g. a
background transaction), starting no later than the log sequence where it began
(run fails otherwise; `run.json` "checkpoints" lists every kept file). After the restart (START at the
offset SCN, since the writer checkpoint is gone) the long transaction commits: diff/replay require every row,
and `exactly_once` fails diff on any transaction delivered twice, except the one committed at the START SCN,
which Debezium 3.6.1 receives again by design (boundary re-send). The trace (16384) in `olr.log` counts the
checkpoint writes.

On images without the fix, run fails the coverage assertion (known_issue): no kept checkpoint is at or
below the client offset. The data still arrives complete (diff/replay PASS) because fork builds b631f01d and
2dfbd467 read V$TRANSACTION on a start without a usable checkpoint and begin at the oldest open transaction;
without that fallback the open transaction's early rows would be lost. The control
(`checkpoint-interval-mb-large`) passes on every image.

Fix (fork fix/checkpoint-interval-zero, 1137a4c4, applies to upstream master): `interval-mb` 0 and
`interval-s` 0 disable their trigger, as `documentation/json/11.state.adoc` says; a log switch or a forced
checkpoint still writes one. With the fix the run passes on both profiles: 2 checkpoints before the restart
instead of ~850, one of them below the client offset with the open transaction as `min-tran`; `fixed_in`
makes a failure there a regression.
