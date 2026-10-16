Savepoint and full rollbacks over every storage shape that changes the redo opcodes of a row
change: migrated rows (PCTFREE 0 growth and shrink), 300-column multi-piece rows, partition row
movement, IOT, CLOB, basic compression, INSERT ALL / INSERT SELECT, MERGE, ON DELETE CASCADE, an
AFTER trigger and FORALL. Also nested savepoints, a savepoint rollback across a log switch, the
same row changed before and inside a savepoint, two sessions interleaving rolled-back work and a
failing multi-row INSERT.

OLR undoes a rolled-back change by matching the undo record against the last buffered change
(`Transaction::rollbackLastOp`). A mismatch logs WARN 70003 (70004 on an empty buffer) and leaves
the change in the transaction, so it is emitted at commit as a phantom. The run check fails on any
such warning and diff fails on a phantom row.
