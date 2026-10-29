Guards the dev stall of 2026-10-28 (fork `fix/queue-checkpoint-stall`), in network mode so CI runs it; the same case
against the real connector is `connect/quiet-checkpoint-stall`.

Debezium 3.7.0 confirms the c_scn of its offset with index 0 and moves the offset only on rows it emits, so its last
transaction stays unconfirmed until a captured table changes again. OLR emits a checkpoint message after every LWN;
without SHOW_CHECKPOINT the writer confirms those itself. Before the fix they were queued behind the held transaction and
could only leave from the head of the queue, so an idle period with more LWNs than queue-size filled the queue and the
writer stopped sending, with no warning. Here: queue-size 200, one captured row, 1 500 single-LWN commits on a table
outside the filter, then one more captured row, which has to arrive within 30 s (a read timeout fails "run").
