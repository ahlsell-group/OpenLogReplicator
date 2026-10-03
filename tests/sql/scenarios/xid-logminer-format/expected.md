# xid-logminer-format

Format option `xid: 3` writes the transaction id the way LogMiner shows it in `V$LOGMNR_CONTENTS.XID`: the raw
bytes of undo segment, slot and sequence. Three transactions store `RAWTOHEX(V$TRANSACTION.XID)` of themselves
in the row they update.

Expected: the `xid` of every event equals the value stored in its row (`xid_column`).

What it guards: the raw bytes follow the byte order of the database host. The container is little-endian, where
OpenLogReplicator 2.0.0 already writes the right value with the JSON format, so this scenario passes there too;
on a big-endian database host (e.g. AIX) 2.0.0 wrote the little-endian order. That case needs a big-endian redo
log and is covered by the unit test `BuilderXidTest`.
