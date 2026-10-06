UPDATE olrt_c2.t SET qty = 11 WHERE id = 1;
COMMIT;
ALTER TABLE olrt_c2.t ADD (newcol VARCHAR2(20), newnum NUMBER(15,3) DEFAULT 0 NOT NULL);
UPDATE olrt_c2.t SET newcol = 'after ddl', newnum = 2.5 WHERE id = 2;
INSERT INTO olrt_c2.t (id, qty, txt, newcol, newnum) VALUES (100, 5, 'new', 'n', 1.250);
COMMIT;
DELETE FROM olrt_c2.t WHERE id = 3;
COMMIT;
