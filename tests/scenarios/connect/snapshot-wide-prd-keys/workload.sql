BEGIN
  FOR i IN 1..1200 LOOP
    INSERT INTO olrt_k1.stream (id, txt) VALUES (i, 'tick ' || i);
    IF MOD(i, 10) = 0 THEN
      UPDATE olrt_k1.wide SET v1 = 'upd ' || i WHERE compid = 'ID' || LPAD(i * 200, 10, '0');
    END IF;
    COMMIT;
    DBMS_SESSION.SLEEP(0.5);
  END LOOP;
END;
/
