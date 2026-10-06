Upstream bersler/OpenLogReplicator#330: OLR 2.0.0 stamps every message
with the transaction's BEGIN SCN (c_scn). The client reconnects with CONTINUE at the
c_scn/c_idx of the last short transaction; the long transaction began before that and
commits after it, so OLR treats it as already delivered and drops it without a log line.
Debezium task restarts while OLR keeps running take exactly this path.
