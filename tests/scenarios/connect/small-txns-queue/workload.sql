BEGIN
  FOR i IN 1 .. 3000 LOOP
    INSERT INTO olrt_mt7.t VALUES (1000 + i, i, 'small ' || i);
    IF MOD(i, 3) = 0 THEN
      UPDATE olrt_mt7.t SET qty = qty + 0.5 WHERE id = 1000 + i - 1;
    END IF;
    COMMIT;
  END LOOP;
END;
/
