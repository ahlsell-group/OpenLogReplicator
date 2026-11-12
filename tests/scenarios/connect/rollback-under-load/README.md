The rollback procedure from OLR back to LogMiner (runbook "Rollback procedure OLR -> LogMiner", dev
rehearsal E11) under load, as close to the real switch as the lab gets. Nine sessions keep writing to a
313-column, a 278-column and a narrow table the whole time: short transactions, savepoint and full
rollbacks, and transactions held open for 20-120 s. Logs switch every 20 s. A 200k-row transaction is open
across the switch and commits only after LogMiner has taken over.

The swap follows the gitops steps with pauses between them: the oldest open transaction is read
from v$transaction, the connector is stopped, the offset is rewound and the connector is resumed on
LogMiner. The PATCH body is the `offsets.json` that `apps/openlogreplicator` renders for
`debezium.alterOffsets` with the SCNs typed as plain numbers. Check `rewind` counts the transactions open
across the old offset SCN (they lose rows in a plain swap). It fails if any of their rows are missing or
if fewer than `min_straddlers` were open. `OLRT_LOAD_SEED` repeats a run.
