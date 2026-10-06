INSERT INTO olrt_gap.t SELECT LEVEL, 'first' FROM dual CONNECT BY LEVEL <= 10;
COMMIT;
-- @switch_logfile
INSERT INTO olrt_gap.t SELECT 100 + LEVEL, 'second' FROM dual CONNECT BY LEVEL <= 10;
COMMIT;
-- @switch_logfile
INSERT INTO olrt_gap.t SELECT 200 + LEVEL, 'third' FROM dual CONNECT BY LEVEL <= 10;
COMMIT;
