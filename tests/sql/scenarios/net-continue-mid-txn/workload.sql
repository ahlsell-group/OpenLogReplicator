-- The client stores its position in the middle of a 1000-row transaction, reads on, is restarted and continues
-- from the stored (c_scn, c_idx).
-- @client connect
INSERT INTO rt_nc.t SELECT 1000 + LEVEL, 'row ' || LEVEL FROM dual CONNECT BY LEVEL <= 1000;
COMMIT;
-- @client read 300
-- @client store
-- @client read 50
-- @client disconnect
-- @client connect
UPDATE rt_nc.t SET v = 'after reconnect' WHERE id = 1;
COMMIT;
