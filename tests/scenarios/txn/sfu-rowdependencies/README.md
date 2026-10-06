Narrows txn/lock-row-trigger (ERROR 50061 too short field supplemental log: 8) to two of its
three ingredients: lock-row records on a ROWDEPENDENCIES table. Each ingredient alone passes.

Cause: the undo of a row lock (5.1, opc 11.1, KDO LKR) on a ROWDEPENDENCIES table carries the
row's dependent SCN in an 8-byte field before the supplemental log fields, like the IRP/DRP/URP
undo of such a table; upstream OpCode0501 reads that field as the supplemental log header. Fixed in fork
branch fix/50061-lkr-rowdeps (`fixed_in` in scenario.toml).
