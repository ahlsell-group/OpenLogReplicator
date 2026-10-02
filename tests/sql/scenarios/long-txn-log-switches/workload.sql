-- One transaction whose redo spans four archived logs, with a short transaction in the middle.
-- @session 1
UPDATE rt_ls.t SET qty = qty + 1 WHERE id <= 5;
-- @switch_logfile
INSERT INTO rt_ls.t SELECT 1000 + LEVEL, LEVEL, 'big ' || LEVEL, DATE '2026-02-01' FROM dual CONNECT BY LEVEL <= 500;
-- @session 2
UPDATE rt_ls.t SET txt = 'short' WHERE id = 10;
COMMIT;
-- @switch_logfile
-- @session 1
DELETE FROM rt_ls.t WHERE id BETWEEN 1001 AND 1100;
-- @switch_logfile
UPDATE rt_ls.t SET qty = 0.000 WHERE id = 6;
COMMIT;
