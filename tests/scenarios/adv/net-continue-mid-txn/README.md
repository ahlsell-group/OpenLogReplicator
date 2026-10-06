Debezium stores (c_scn, c_idx) of the last event it wrote, which can be in the middle of a large
transaction (batch jobs can write hundreds of thousands of rows in one). After a task restart it sends CONTINUE with that position.
The rest of the transaction must arrive (idx > c_idx), nothing before it twice, nothing lost; once with
a CONFIRM sent at the stored position and once with a later position that was never confirmed
(Debezium's confirm rule skips equal SCNs). The transaction mixes 3 000 narrow inserts with 300 inserts
and updates of 300-column (two-piece) rows.
