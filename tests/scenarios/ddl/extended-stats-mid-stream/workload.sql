UPDATE olrt_ddl2.t SET qty = 2 WHERE id = 1;
COMMIT;
BEGIN DBMS_OUTPUT.PUT_LINE(DBMS_STATS.CREATE_EXTENDED_STATS('OLRT_DDL2', 'T', '(QTY,TXT)')); END;
/
ALTER TABLE olrt_ddl2.t ADD (late VARCHAR2(10));
UPDATE olrt_ddl2.t SET late = 'late', qty = 3 WHERE id = 2;
INSERT INTO olrt_ddl2.t (id, qty, txt, late) VALUES (100, 1, 'x', 'y');
COMMIT;
