# net-long-txn-continue

Network writer. Session 2 begins a transaction (insert 100) and keeps it open. Session 1 commits two short
transactions; after each one the client stores the position of the commit message and confirms it. The client
is restarted and continues from the stored position; then session 2 inserts 101 and commits.

Expected: the long transaction arrives after the restart with both rows (events c=3, u=1 in total).

What it guards: messages are sent in commit order, so their position (`c_scn`, `c_idx`) has to grow in that
order. OpenLogReplicator 2.0.0 positioned the messages of a transaction by its begin, so the long transaction
came after the short ones with a lower `c_scn`; after CONTINUE from the confirmed position it was taken for
already delivered and skipped: rows 100 and 101 are missing from the replay.
