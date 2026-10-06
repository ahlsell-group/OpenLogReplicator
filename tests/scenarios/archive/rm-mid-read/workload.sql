-- archived log #0 of the recording: small transactions
BEGIN
  FOR t IN 1..5 LOOP
    INSERT INTO olrt_av.t SELECT t * 100 + LEVEL, 'before', 'b' FROM dual CONNECT BY LEVEL <= 10;
    COMMIT;
  END LOOP;
END;
/
-- @switch_logfile
-- archived log #1 (the victim): ~8 MB of redo in 8 transactions
BEGIN
  FOR t IN 1..8 LOOP
    INSERT INTO olrt_av.t SELECT 100000 + t * 10000 + LEVEL, 'victim', RPAD('v', 200, 'v') FROM dual CONNECT BY LEVEL <= 3200;
    COMMIT;
  END LOOP;
END;
/
-- @switch_logfile
-- archived log #2: small transactions after the victim
BEGIN
  FOR t IN 1..5 LOOP
    INSERT INTO olrt_av.t SELECT 900000 + t * 100 + LEVEL, 'after', 'a' FROM dual CONNECT BY LEVEL <= 10;
    COMMIT;
  END LOOP;
END;
/
