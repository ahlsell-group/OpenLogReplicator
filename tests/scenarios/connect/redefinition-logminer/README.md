`connect/redefinition` on Debezium's LogMiner adapter (hybrid strategy, IN filter, as the prd connector).
Two failures, both versions: the refresh that SYNC/FINISH apply to the interim table (DELETE + INSERT on
T_INT1, whose object becomes T at the swap) is published as changes to T, with the pre-refresh values as
before images; then mining hits a DELETE on MLOG$_T, which FINISH dropped, and the task fails with "Failed to
resolve a table for object id ...". Rows after that are not delivered. The redo dictionary (harness
reference) shows the objects: in the recorded run obj 73362 = T_INT1 (later T), 73367 = MLOG$_T.
Minimal repros: `redefinition-logminer-min` (phantom refresh), `redefinition-logminer-widen-min`
(layout change: the task fails on the first UPDATE after the swap).
