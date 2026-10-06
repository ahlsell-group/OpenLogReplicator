INSERT INTO olrt_ddl5.t (qty, gone) VALUES (1, 'x');
INSERT INTO olrt_ddl5.t (qty, dflt) VALUES (2, NULL);
COMMIT;
ALTER TABLE olrt_ddl5.t SET UNUSED (gone);
INSERT INTO olrt_ddl5.t (qty) VALUES (3);
UPDATE olrt_ddl5.t SET qty = qty + 10 WHERE qty < 3;
COMMIT;
