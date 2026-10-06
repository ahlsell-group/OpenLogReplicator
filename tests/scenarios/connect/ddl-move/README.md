`ALTER TABLE MOVE` + index rebuild, then DML. With flags 32 OLR emits two DDL messages with binary
garbage in owner/table/sql before the real MOVE. Passes when the task stays RUNNING and
the DML after the MOVE arrives intact.
