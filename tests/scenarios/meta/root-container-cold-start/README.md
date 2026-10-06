# meta/root-container-cold-start

Regression test for a stall on a non-CDB: after a Debezium START without a usable checkpoint,
an affected OLR reads redo indefinitely, logs `WARN 60010 no match found for transaction
rollback` for every transaction with a rollback and emits nothing.

Root cause: `ReplicatorOnline::loadDatabaseMetadata` reads the pdb id with
`(SELECT P.DBID FROM SYS.V_$PDBS P WHERE P.CON_ID = SYS_CONTEXT('USERENV','CON_ID'))`. On a
non-CDB that is NULL, the OCI define has no indicator, ORA-01405 is swallowed and
the uninitialized local `dbId` is copied into `metadata->dbId`. When the garbage is non-zero,
`Parser::appendToTransactionBegin` skips every begin, so no transaction is ever tracked.

The lab database is a CDB, so this scenario runs everything in CDB$ROOT (`container = "root"`):
`SYS.V_$PDBS` has no row for the root either, which gives OLR the same NULL. A checkpoint file
would restore `"db-id":0` and hide the bug, hence the cold START.

Expected: three transactions delivered (insert, insert with a partial rollback, update), no
60010. A build with the bug fails `run` (unexpected WARN 60010) and `diff`/`replay` (no events).
Whether an unfixed build fails depends on what the stack holds at that point; a build compiled
with `-ftrivial-auto-var-init=pattern` makes it deterministic.
