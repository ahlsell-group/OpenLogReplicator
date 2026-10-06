# root-container-cold-start

Everything runs in CDB$ROOT: the table belongs to a common user, OLR logs in to the root service as the common
user `C##RT_OLR` and starts without a checkpoint. Three transactions: an insert, an insert with a partial
rollback (`ROLLBACK TO SAVEPOINT`), an update.

Expected: all three transactions (events c=2, u=1), no warning.

What it guards: at startup OLR reads the pdb id with a subquery on `SYS.V_$PDBS`. On a non-CDB, and in
CDB$ROOT, that view has no row and the value is NULL; the OCI define had no indicator and the variable was not
initialised, so the pdb id was whatever the stack held. A non-zero value made the parser skip every
transaction begin: nothing was sent and every rollback logged `WARN 60010 no match found for transaction
rollback`. A checkpoint restores pdb id 0 and hides the problem, so it shows on a cold start only. Whether a
build without the fix fails depends on the stack contents, i.e. on the compiler and the build; a build with
`-ftrivial-auto-var-init=pattern` fails every time.

The lab database is a CDB; the root container stands in for a non-CDB here.
