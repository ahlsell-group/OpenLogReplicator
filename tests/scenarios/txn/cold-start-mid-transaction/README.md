Without a checkpoint OLR starts at the redo log containing the START
SCN; a transaction that began in an earlier log has no BEGIN, and at commit OLR logs
WARN 60011 skipping transaction with no beginning and drops all of it, including the rows
written after START. Happens at every first start after a Debezium snapshot. The fork's
fix (fix/cold-start-low-watermark) reads V$TRANSACTION to start earlier.
