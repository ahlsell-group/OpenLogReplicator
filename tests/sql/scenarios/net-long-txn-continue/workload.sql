-- A long transaction is open while two short ones commit and are confirmed; the client is restarted before the
-- long one commits.
-- @client connect
-- @session 2
INSERT INTO rt_lo.t VALUES (100, 'long 1');
-- @sleep 1
-- @session 1
UPDATE rt_lo.t SET v = 'short 1' WHERE id = 1;
COMMIT;
-- @client commits 1
-- @client store
-- @client confirm
INSERT INTO rt_lo.t VALUES (2, 'short 2');
COMMIT;
-- @client commits 1
-- @client store
-- @client confirm
-- @client disconnect
-- @client connect
-- @session 2
INSERT INTO rt_lo.t VALUES (101, 'long 2');
COMMIT;
