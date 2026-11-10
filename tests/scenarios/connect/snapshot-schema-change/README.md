# connect/snapshot-schema-change

ALTER TABLE ADD COLUMN on the table being snapshotted while the snapshot is paused (64-row chunks, 60000 rows; pause-snapshot after the first 5000 records, resume once the DDL has streamed): the key columns must be resolved again for the new table model and every read after the change must carry the new column.
