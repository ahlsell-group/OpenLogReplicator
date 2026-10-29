Reproduces the dev stall of 2026-10-28 (OLR 2.0.0-ahlsell.8, Debezium 3.7.0 OLR adapter, queue-size 1 000 000).

Debezium confirms the SCN of its committed offset with index 0, which keeps the last transaction it emitted
held in OpenLogReplicator for a replay. The network writer puts every message into its queue, including the
checkpoint message the parser emits after each LWN; without SHOW_CHECKPOINT those are confirmed locally but
leave the queue only from its head, so they pile up behind the held transaction. Once a quiet period has more
LWNs than queue-size the writer waits for confirmations that never come, the builder runs out of memory and
the parser stops. Nothing is logged: WARN 60040 only fires for a queue holding one transaction.

Here: queue-size 200, one captured row, 1 500 single-LWN commits on a table outside the filter, then one more
captured row. On an affected build the last row never reaches Kafka (delivery fails) and the task stays RUNNING.
