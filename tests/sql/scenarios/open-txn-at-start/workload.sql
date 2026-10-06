-- A transaction begins two redo logs before OLR is started (no checkpoint, start-scn) and is still open then.
-- @session 2
INSERT INTO rt_ot.t VALUES (100, 'before start 1');
-- @switch_logfile
INSERT INTO rt_ot.t VALUES (101, 'before start 2');
-- @switch_logfile
-- @start
-- @session 1
UPDATE rt_ot.t SET v = 'short' WHERE id = 1;
COMMIT;
-- @session 2
INSERT INTO rt_ot.t VALUES (102, 'after start');
UPDATE rt_ot.t SET v = 'changed after start' WHERE id = 100;
COMMIT;
