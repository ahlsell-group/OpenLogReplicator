BEGIN
  FOR i IN 1001..1060 LOOP
    INSERT INTO olrt_k3.stream (id, txt) VALUES (i, 'tick ' || i);
    COMMIT;
    DBMS_SESSION.SLEEP(0.5);
  END LOOP;
END;
/
