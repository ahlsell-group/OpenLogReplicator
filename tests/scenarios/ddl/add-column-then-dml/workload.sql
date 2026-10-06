UPDATE olrt_ddl1.t SET qty = 1 WHERE id = 1;
COMMIT;
ALTER TABLE olrt_ddl1.t ADD (newcol VARCHAR2(20), newnum NUMBER(15,3));
UPDATE olrt_ddl1.t SET newcol = 'after ddl', newnum = 0.000 WHERE id = 2;
INSERT INTO olrt_ddl1.t (id, qty, txt, newcol, newnum) VALUES (100, 5, 'new', 'n', 1.250);
COMMIT;
DELETE FROM olrt_ddl1.t WHERE id = 3;
COMMIT;
