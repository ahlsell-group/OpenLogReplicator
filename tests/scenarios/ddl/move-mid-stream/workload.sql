UPDATE olrt_ddl6.t SET qty = 5 WHERE id = 1;
COMMIT;
ALTER TABLE olrt_ddl6.t MOVE;
ALTER INDEX olrt_ddl6.t_pk REBUILD;
UPDATE olrt_ddl6.t SET qty = 6 WHERE id = 2;
DELETE FROM olrt_ddl6.t WHERE id = 3;
COMMIT;
