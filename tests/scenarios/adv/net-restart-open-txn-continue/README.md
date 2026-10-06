Begin-SCN stamping (upstream bersler/OpenLogReplicator#330) and the cold-start boundary at awkward
moments with the writer checkpoint kept (Debezium CONTINUE path, which a task-only restart or an OLR
restart that keeps the writer checkpoint takes): a long transaction begins with one
two-piece row (300 columns), OLR is stopped cleanly right after that BEGIN, more rows and a log switch
follow, short transactions commit and are confirmed, OLR is killed (SIGKILL) mid-transaction, the long
transaction updates the high column of its row and commits. Every row must arrive.
