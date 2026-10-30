`DBMS_REDEFINITION` of a captured table (QTY `NUMBER(15,3)` -> `NUMBER(18,4)`, EXTRA new), post-swap rows,
then `DROP TABLE`. OLR is stopped across all of it, so the connector reads the post-swap rows when the table
no longer exists and its model cannot be read from the database. The replay check has no table to compare
with at the end.
