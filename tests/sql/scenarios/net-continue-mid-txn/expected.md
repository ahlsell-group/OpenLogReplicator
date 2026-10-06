# net-continue-mid-txn

Network writer. The client starts at the start SCN; one transaction inserts 1000 rows. The client receives
300 messages (the begin and 299 inserts), stores the position of the last one (`c_scn`, `c_idx` from its
header, as Debezium stores it in its offset), receives 50 more, and is restarted: those 50 are lost on the
client side. It sends CONTINUE with the stored position, then an update commits.

Expected: OLR resends from the message right after the stored one. The stored messages plus the resent ones
contain every insert exactly once and the update (events c=1000, u=1).

What it guards: the header's `c_idx` must be the index OLR itself compares in CONTINUE and CONFIRM. In
OpenLogReplicator 2.0.0 the header carried the index of the next message, so a client continuing from the
`c_idx` of the last message it stored never got the message after it: one row of the transaction is missing
from the replay (upstream issue #325).
