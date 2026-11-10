# connect/snapshot-schema-change

ALTER TABLE ADD COLUMN on the table being snapshotted, between chunks (1024-row chunks, 60000 rows): the key columns must be resolved again for the new table model and every row must arrive with the new column.
