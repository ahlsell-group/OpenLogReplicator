TRUNCATE produces no row events. Only a DDL message (flags SHOW_DDL, json profile) tells a
consumer the rows are gone, so replay is expected to fail on the debezium profile (flags 0). TRUNCATE also changes dataobj#; inserts after it must still be decoded.
