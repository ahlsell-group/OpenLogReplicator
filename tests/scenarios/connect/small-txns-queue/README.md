3 000 committed transactions of one or two rows each (about 7 000 messages with begin and commit) through an OLR
queue-size of 2000, then a task restart and one more row. Every transaction is smaller than the queue, so every
Debezium version must pass; it guards that confirming inside transactions changes nothing for ordinary traffic.
Checks: every row exactly once, one BEGIN and one END per transaction with correct counts.
