-- @session 2
DECLARE rc PLS_INTEGER;
BEGIN
  -- two-phase commit
  rc := sys.dbms_xa.xa_start(sys.dbms_xa_xid(1001), sys.dbms_xa.TMNOFLAGS);
  INSERT INTO olrt_xa.t VALUES (10, 'xa-2pc');
  UPDATE olrt_xa.t SET txt = 'xa-upd' WHERE id = 1;
  rc := sys.dbms_xa.xa_end(sys.dbms_xa_xid(1001), sys.dbms_xa.TMSUCCESS);
  rc := sys.dbms_xa.xa_prepare(sys.dbms_xa_xid(1001));
  -- a second branch prepared then rolled back
  rc := sys.dbms_xa.xa_start(sys.dbms_xa_xid(1002), sys.dbms_xa.TMNOFLAGS);
  INSERT INTO olrt_xa.t VALUES (20, 'xa-rollback');
  rc := sys.dbms_xa.xa_end(sys.dbms_xa_xid(1002), sys.dbms_xa.TMSUCCESS);
  rc := sys.dbms_xa.xa_prepare(sys.dbms_xa_xid(1002));
  -- a one-phase branch
  rc := sys.dbms_xa.xa_start(sys.dbms_xa_xid(1003), sys.dbms_xa.TMNOFLAGS);
  INSERT INTO olrt_xa.t VALUES (30, 'xa-1pc');
  rc := sys.dbms_xa.xa_end(sys.dbms_xa_xid(1003), sys.dbms_xa.TMSUCCESS);
  rc := sys.dbms_xa.xa_commit(sys.dbms_xa_xid(1003), TRUE);
END;
/
-- @session 1
INSERT INTO olrt_xa.t VALUES (40, 'local-between');
COMMIT;
-- @session 3
DECLARE rc PLS_INTEGER;
BEGIN
  rc := sys.dbms_xa.xa_commit(sys.dbms_xa_xid(1001), FALSE);
  rc := sys.dbms_xa.xa_rollback(sys.dbms_xa_xid(1002));
END;
/
