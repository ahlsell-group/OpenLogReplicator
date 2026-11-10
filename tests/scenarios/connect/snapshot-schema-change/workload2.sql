BEGIN
  FOR i IN 5..150 LOOP
    INSERT INTO olrt_k2.stream (id, txt) VALUES (i, 'tick ' || i);
    COMMIT;
    DBMS_SESSION.SLEEP(0.5);
  END LOOP;
END;
/
