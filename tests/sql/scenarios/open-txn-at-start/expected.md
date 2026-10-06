# open-txn-at-start

Session 2 inserts rows 100 and 101 in one transaction, spread over two redo logs, and leaves it open. Then
the start SCN is taken and OLR is started (online reader, `start-scn`, no checkpoint). Session 1 commits a short
update; session 2 inserts row 102, updates row 100 and commits.

Expected: both transactions in full. The open transaction committed after the start SCN, so all of it is sent:
inserts 100, 101, 102 and the update of 100 (events c=3, u=2), and its begin message carries an `scn` not below
the start SCN.

What it guards:

- Starting without a checkpoint, the reader began with the redo log of the start SCN and never read the begin
  of a transaction which was open then; at commit the whole transaction was dropped with
  `WARN 60011 skipping transaction with no beginning`, including the changes made after the start SCN. The
  replay misses rows 100-102 (OpenLogReplicator 2.0.0).
- Once the begin is read, the begin message and the changes made before the start SCN must not be sent with an
  `scn` below it: a client which starts at that SCN (Debezium) drops such messages, so the transaction would
  reach it without its begin and first rows. The runner checks every `scn` field against the start SCN.

The runner starts OLR at the `-- @start` line while session 2 is still open, so OLR sees the transaction in
`V$TRANSACTION` (granted to the OLR user by `docker/init/02-olr-user.sql`).
