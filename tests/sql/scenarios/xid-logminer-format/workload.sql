-- Each transaction stores its own V$TRANSACTION.XID (the value LogMiner shows as XID) in the row it changes.
INSERT INTO rt_xid.t VALUES (1, NULL);
UPDATE rt_xid.t SET tx = (SELECT RAWTOHEX(x.xid) FROM v$transaction x
                           WHERE x.addr = (SELECT s.taddr FROM v$session s WHERE s.sid = SYS_CONTEXT('USERENV', 'SID')))
 WHERE id = 1;
COMMIT;
INSERT INTO rt_xid.t VALUES (2, NULL);
UPDATE rt_xid.t SET tx = (SELECT RAWTOHEX(x.xid) FROM v$transaction x
                           WHERE x.addr = (SELECT s.taddr FROM v$session s WHERE s.sid = SYS_CONTEXT('USERENV', 'SID')))
 WHERE id = 2;
COMMIT;
-- @session 2
INSERT INTO rt_xid.t VALUES (3, NULL);
UPDATE rt_xid.t SET tx = (SELECT RAWTOHEX(x.xid) FROM v$transaction x
                           WHERE x.addr = (SELECT s.taddr FROM v$session s WHERE s.sid = SYS_CONTEXT('USERENV', 'SID')))
 WHERE id = 3;
COMMIT;
