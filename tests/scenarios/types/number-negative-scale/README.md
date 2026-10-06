NUMBER(p,-s) rounds to hundreds. Upstream bersler/OpenLogReplicator#329 reports a crash at
dictionary load for negative scale. The wide/ tables have no such column; this guards against
a source schema adding one.
