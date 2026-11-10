BEGIN
  FOR i IN 1..4 LOOP
    INSERT INTO olrt_k6.stream (id, txt) VALUES (i, 'tick ' || i);
    COMMIT;
    DBMS_SESSION.SLEEP(0.5);
  END LOOP;
END;
/
ALTER TABLE olrt_k6.t ADD (newcol VARCHAR2(20));
UPDATE olrt_k6.t SET newcol = 'after ddl' WHERE MOD(TO_NUMBER(SUBSTR(compid, 3)), 1000) = 0;
COMMIT;
BEGIN
  FOR i IN 5..150 LOOP
    INSERT INTO olrt_k6.stream (id, txt) VALUES (i, 'tick ' || i);
    COMMIT;
    DBMS_SESSION.SLEEP(0.5);
  END LOOP;
END;
/
