CREATE TABLE olrt_ddl4.newt (id NUMBER(10) PRIMARY KEY, v VARCHAR2(20), q NUMBER(15,3));
ALTER TABLE olrt_ddl4.newt ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
INSERT INTO olrt_ddl4.newt VALUES (1, 'first', 0.000);
INSERT INTO olrt_ddl4.newt VALUES (2, 'second', 2.500);
UPDATE olrt_ddl4.t SET txt = 'old table' WHERE id = 1;
COMMIT;
UPDATE olrt_ddl4.newt SET v = 'upd' WHERE id = 1;
COMMIT;
