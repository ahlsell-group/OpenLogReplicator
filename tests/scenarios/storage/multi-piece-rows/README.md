Every row of `WIDE_C` (300 columns, PCTFREE 0) is stored in two row pieces (Oracle splits rows of more than
255 columns). The workload changes the first piece only, the second only and both; sets values to NULL and back;
runs a no-op `SET c = c`; grows 51 rows until they migrate to another block and updates one of them again;
deletes three rows and inserts a sparse row with only a few columns set.

OLR must give one event per changed row with the full before and after image (diff against LogMiner), and
the replayed table must equal the database.
