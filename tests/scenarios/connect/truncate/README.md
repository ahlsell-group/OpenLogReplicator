TRUNCATE between DML. Replay passes only if a truncate event (`op: t`) reaches the topic. Needs
OLR flags 32 to see the DDL at all, and Debezium skips truncate events unless `skipped.operations`
excludes `t` (default `t`, which the default connector config keeps). `truncate-not-skipped` is the same with
`skipped.operations = none`.
