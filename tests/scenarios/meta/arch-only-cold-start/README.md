With "flags": 1 and an empty state dir, OLR never reads an
online log, so it never learns the resetlogs id; its V$ARCHIVED_LOG query filters on
RESETLOGS_ID = 0 and finds nothing. It logs "no redo logs to process" forever (timeout here).

Cause (fork fix/arch-only-resetlogs, 8ee9c541): `Replicator::run` loads the role and the incarnation list
only inside `updateOnlineRedoLogData`, which is skipped with flags 1. `metadata->resetlogs` stays 0, the
sequence lookup and the archived log list both filter on `RESETLOGS_ID = 0`, and the replicator never finds
a log. The fix reads the incarnation at startup with flags 1 as well (the online log list is still skipped).
A build with the fix logs "current resetlogs is: ..." and reads the archived log at once.
