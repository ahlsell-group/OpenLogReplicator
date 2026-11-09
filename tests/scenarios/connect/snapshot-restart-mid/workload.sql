BEGIN
  FOR i IN 1..40 LOOP
    INSERT INTO olrt_k4.stream (id, txt) VALUES (i, 'tick ' || i);
    IF MOD(i, 10) = 0 THEN
      UPDATE olrt_k4.t SET v1 = 'upd ' || i WHERE compid = 'ID' || LPAD(i * 1000, 10, '0');
    END IF;
    COMMIT;
    DBMS_SESSION.SLEEP(0.5);
  END LOOP;
END;
/
