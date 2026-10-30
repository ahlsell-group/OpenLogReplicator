Debezium on the OLR adapter while two `DBMS_REDEFINITION`s run on the captured table: first storage only
(PCTFREE/INITRANS, with a scheduler job committing DML during the copy), then a layout change (TXT and D
before QTY, NUMBER(15,3) -> NUMBER(18,4), VARCHAR2(40) -> VARCHAR2(80), EXTRA new). OLR follows both swaps
(`ddl/redefinition-*` pass). Debezium delivers every row of the storage-only redefinition with no loss or
duplicate, and the interim copy does not leak. After the layout change it keeps the old table model: OLR
sends no DDL for T (FINISH has none naming it; with flags 32 the DDL messages are for MLOG$_T and T_INT
only), so QTY values with 4 decimals arrive as null and EXTRA never reaches the topic schema. Minimal repro:
`redefinition-widen-min`.
