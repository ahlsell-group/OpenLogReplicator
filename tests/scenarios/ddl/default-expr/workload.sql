INSERT INTO olrt_ddl12.t (v) VALUES ('a');
INSERT INTO olrt_ddl12.t (qty, v) VALUES (5, NULL);
INSERT INTO olrt_ddl12.t (id) VALUES (500);
ALTER TABLE olrt_ddl12.t MODIFY (qty DEFAULT 1.500, v DEFAULT 'changed');
INSERT INTO olrt_ddl12.t (id) VALUES (501);
UPDATE olrt_ddl12.t SET v = DEFAULT WHERE id = 500;
INSERT INTO olrt_ddl12.t (id) VALUES (502);
COMMIT;
