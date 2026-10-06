-- @session 1
UPDATE olrt_il.t SET qty = 100 WHERE id = 1;
-- @session 2
UPDATE olrt_il.t SET qty = 200 WHERE id = 2;
-- @session 1
INSERT INTO olrt_il.t VALUES (101, 1, 'T1', NULL);
-- @session 2
INSERT INTO olrt_il.t VALUES (201, 2, 'T2', NULL);
COMMIT;
-- @session 1
UPDATE olrt_il.t SET qty = 101 WHERE id = 1;
COMMIT;
-- @session 3
DELETE FROM olrt_il.t WHERE id = 3;
-- @session 2
UPDATE olrt_il.t SET qty = 300 WHERE id = 4;
COMMIT;
-- @session 3
COMMIT;
