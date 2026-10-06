UPDATE olrt_ddl3.t SET qty = 9 WHERE id = 1;
COMMIT;
TRUNCATE TABLE olrt_ddl3.t;
INSERT INTO olrt_ddl3.t VALUES (1, 1, 'after truncate', NULL);
INSERT INTO olrt_ddl3.t VALUES (2, 2, 'after truncate', NULL);
COMMIT;
