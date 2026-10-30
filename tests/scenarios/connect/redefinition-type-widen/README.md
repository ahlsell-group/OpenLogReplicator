`DBMS_REDEFINITION` that changes only non-NUMBER types: `TS DATE` -> `TIMESTAMP(6)` and `CODE VARCHAR2(10)` ->
`VARCHAR2(50)`, no new column. Rows before, during and after the swap.

The other columns do not change, but OpenLogReplicator describes them differently from Debezium's model: lengths
in bytes for `VARCHAR2(10 CHAR)` (40), `NVARCHAR2(10)` (20) and `NCHAR(3)` (6), no precision for `NUMBER(*,2)`
and `INTEGER`, `FLOAT` as `number(126,-1)`, and `TIMESTAMP(3)` only as "has a fraction". A connector must not
reload the model for them: the pre-swap rows must not log "does not match the change".

The type changes are chosen so that the Schema Registry's default BACKWARD compatibility accepts the reloaded
value schema: `DATE` and `TIMESTAMP(6)` are both Avro `long` (`io.debezium.time.Timestamp` in milliseconds,
`io.debezium.time.MicroTimestamp` in microseconds; only the `connect.name` property differs), and both
`VARCHAR2` lengths are Avro `string`. A change of Avro type, such as `NUMBER(10)` -> `VARCHAR2(20)`
(`connect/redefinition-type-only`), is rejected and stops the task.

A connector that keeps the old model converts the post-swap timestamps as `DATE` (milliseconds, the
microseconds lost); one that reloads it emits them as microseconds. `CODE` values are strings either way, so
its widening shows only in the schema history.
