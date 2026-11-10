BEGIN
  FOR i IN 1..240 LOOP
    INSERT INTO olrt_k5.stream (id, txt) VALUES (i, 'tick ' || i);
    IF MOD(i, 10) = 0 THEN
      UPDATE olrt_k5.t SET v1 = 'upd ' || i WHERE MOD(TO_NUMBER(SUBSTR(compid, 3)), 97) = MOD(i, 97);
    END IF;
    COMMIT;
    DBMS_SESSION.SLEEP(0.25);
  END LOOP;
END;
/
