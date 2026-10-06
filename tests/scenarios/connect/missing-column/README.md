A column Debezium knows but OLR does not send becomes null, and for a NOT NULL column
the type default (`""`, `0`, epoch). TPK has only PK supplemental logging, so an UPDATE's redo has
the key and the changed columns only; TV has two NOT NULL virtual columns, which are in JDBC
metadata but never in redo. Evidence is in kafka.jsonl (`before`/`after` per record); replay and
diff fail where a substituted value lands in a row image.
