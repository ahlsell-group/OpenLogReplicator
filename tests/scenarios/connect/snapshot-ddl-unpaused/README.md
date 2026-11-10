# connect/snapshot-ddl-unpaused

ALTER TABLE ADD COLUMN on the table being snapshotted while the snapshot runs (64-row chunks, 60000 rows, the DDL after the first 5000 records): records what Debezium does with a chunk whose result set has a column its table model does not have yet.
