# connect/snapshot-multi-table-surrogate

One incremental snapshot request for two tables, one of them without a primary key (keyed by message.key.columns), then a second request for the first table with a surrogate key: the key columns follow the table and the surrogate key.
