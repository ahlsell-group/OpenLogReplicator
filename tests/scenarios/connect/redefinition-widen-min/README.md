Smallest shape of the `connect/redefinition` finding: one row, `DBMS_REDEFINITION` from (ID, QTY NUMBER(15,3))
to (ID, QTY NUMBER(18,4), EXTRA), then an UPDATE setting QTY = 1.2345 and an INSERT. OLR decodes both rows
with the new dictionary, but Debezium's OLR adapter changes its table model only on a DDL message for the
table, and the swap has none (also with flags 32). It logs "Failed to properly convert data value for
...QTY of type NUMBER" and writes QTY = null; 2.5 fits scale 3 and survives; EXTRA is never in the schema.
