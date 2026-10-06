ADD COLUMN (one nullable, one NOT NULL DEFAULT 0) mid-stream, then DML that sets both. Debezium's
OLR adapter only changes its table model on DDL messages, which OLR sends with flags 32 only.
With flags 0 the new columns are expected to be missing from the topic (replay/diff fail); with
`debezium-ddl` they should appear in the Avro schema (run.json `topic_schemas`) and the values match.
