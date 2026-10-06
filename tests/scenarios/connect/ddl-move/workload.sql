UPDATE olrt_c5.t SET qty = 5 WHERE id = 1;
COMMIT;
ALTER TABLE olrt_c5.t MOVE;
ALTER INDEX olrt_c5.t_pk REBUILD;
UPDATE olrt_c5.t SET qty = 6 WHERE id = 2;
DELETE FROM olrt_c5.t WHERE id = 3;
COMMIT;
INSERT INTO olrt_c5.t VALUES (11, 11, 'after move');
COMMIT;
