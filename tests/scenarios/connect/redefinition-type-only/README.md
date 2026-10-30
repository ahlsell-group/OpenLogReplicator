`DBMS_REDEFINITION` that changes only column types: `QTY NUMBER(15,3)` -> `NUMBER(18,4)` and
`CODE NUMBER(10)` -> `VARCHAR2(20)`, no new column. Rows before and after the swap. A connector that keeps
the old model emits QTY and CODE as null after the swap; one that reloads it changes CODE from `long` to
`string` in the topic's value schema, which the Schema Registry's default BACKWARD compatibility rejects.
