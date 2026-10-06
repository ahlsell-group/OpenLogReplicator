-- @session 1
UPDATE olrt_ls.t SET qty = qty + 1 WHERE id <= 5;
-- @switch_logfile
INSERT INTO olrt_ls.t SELECT 1000 + LEVEL, LEVEL, 'big ' || LEVEL, SYSDATE FROM dual CONNECT BY LEVEL <= 500;
-- @session 2
UPDATE olrt_ls.t SET txt = 'short' WHERE id = 10;
COMMIT;
-- @switch_logfile
-- @session 1
DELETE FROM olrt_ls.t WHERE id BETWEEN 1001 AND 1100;
-- @switch_logfile
UPDATE olrt_ls.t SET qty = 0.000 WHERE id = 6;
COMMIT;
