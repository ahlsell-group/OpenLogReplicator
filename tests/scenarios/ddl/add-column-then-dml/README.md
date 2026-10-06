The schema changes inside the captured range. Rows written after the DDL must carry the new
column. OLR emits the DDL itself only with flags SHOW_DDL (32): the json profile sets it,
the debezium profile uses flags 0, so compare the two columns of the results.
Debezium's OLR adapter learns about new columns only from those DDL messages.
