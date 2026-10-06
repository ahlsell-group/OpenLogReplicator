UPDATE olrt_c4.t SET qty = 9 WHERE id = 1;
COMMIT;
TRUNCATE TABLE olrt_c4.t;
INSERT INTO olrt_c4.t VALUES (1, 1, 'after truncate');
INSERT INTO olrt_c4.t VALUES (2, 2, 'after truncate');
COMMIT;
