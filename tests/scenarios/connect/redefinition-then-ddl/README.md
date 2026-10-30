`DBMS_REDEFINITION` of a captured table (QTY `NUMBER(15,3)` -> `NUMBER(18,4)`, EXTRA new), then
`MODIFY (qty NUMBER(20,6))` and `DROP COLUMN extra` on the same table. OLR is stopped across all of it, so
the connector reads the first post-swap row only after both DDLs: a model reloaded from the database at
that point has QTY `NUMBER(20,6)` and no EXTRA, while the rows describe QTY `NUMBER(18,4)` and carry EXTRA.
