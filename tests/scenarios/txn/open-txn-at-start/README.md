Session 2 inserts rows 100 and 101 in one transaction, spread over two redo logs, and leaves it open. Then
the client connects with a cold START at the current SCN (no checkpoint). Session 1 commits a short update;
session 2 inserts row 102, updates row 100 and commits.

Expected: both transactions in full. The open transaction committed after the start SCN, so all of it is
sent (inserts 100, 101, 102 and the update of 100), and its begin message and rows carry an `scn` not below
the start SCN. The emulated client drops messages with an `scn` below its START SCN, as Debezium does, so a
row stamped too low shows up as missing in diff and replay.

What it guards: starting without a checkpoint, upstream 2.0.0 begins with the redo log of the start SCN and
never reads the begin of a transaction open at that point; at commit the whole transaction is dropped with
`WARN 60011 skipping transaction with no beginning`. Once the begin is read, the messages must not carry an
`scn` below the start SCN. Fork branches `fix/cold-start-low-watermark` and `fix/start-boundary-scn`.
Complements `txn/cold-start-mid-transaction` (one open row before START).
