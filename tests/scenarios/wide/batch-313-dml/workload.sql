-- 1) INSERT ALL into both tables, 3 columns each (the other 310 are NULL)
INSERT ALL
  INTO olrt_wb.t (id, c2, c3, c313) VALUES (id, DATE '2026-05-05', 'all', 13.130)
  INTO olrt_wb.t2 (id, c2, c3, c4) VALUES (id, DATE '2026-05-06', 'all2', 4.000)
SELECT 1000 + LEVEL id FROM dual CONNECT BY LEVEL <= 400;
-- 2) INSERT .. SELECT of 600 full rows
DECLARE s VARCHAR2(32000) := 'INSERT INTO olrt_wb.t2 SELECT LEVEL + 5000';
BEGIN
  FOR i IN 2 .. 313 LOOP
    s := s || ', ' || CASE MOD(i, 3) WHEN 0 THEN '''w'' || LEVEL' WHEN 1 THEN 'MOD(LEVEL, 7) + 0.125' ELSE 'DATE ''2025-12-31'' + MOD(LEVEL, 50)' END;
  END LOOP;
  EXECUTE IMMEDIATE s || ' FROM dual CONNECT BY LEVEL <= 600';
END;
/
-- 3) MERGE: update low, middle and high columns, insert new rows
MERGE INTO olrt_wb.t d USING (SELECT LEVEL + 250 id, 'm' || LEVEL v FROM dual CONNECT BY LEVEL <= 100) s ON (d.id = s.id)
WHEN MATCHED THEN UPDATE SET d.c3 = s.v, d.c150 = s.v, d.c312 = s.v, d.c313 = 99.5
WHEN NOT MATCHED THEN INSERT (id, c3, c150, c312) VALUES (s.id, s.v, s.v, s.v);
-- 4) FORALL update of high columns
DECLARE
  TYPE nt IS TABLE OF NUMBER;
  ids nt := nt();
BEGIN
  FOR i IN 1 .. 200 LOOP ids.EXTEND; ids(i) := i; END LOOP;
  FORALL i IN 1 .. ids.COUNT UPDATE olrt_wb.t SET c309 = 'fa' || ids(i), c311 = DATE '2030-01-01', c313 = ids(i) / 8 WHERE id = ids(i);
END;
/
-- 5) update of the primary key and of a NUMBER column to NULL
UPDATE olrt_wb.t SET id = id + 100000 WHERE id BETWEEN 1 AND 20;
UPDATE olrt_wb.t SET c4 = NULL, c5 = NULL WHERE id BETWEEN 21 AND 60;
-- 6) update only setting NULLs that already are NULL (no net change) and empty string
UPDATE olrt_wb.t SET c200 = NULL, c201 = NULL WHERE id BETWEEN 1001 AND 1100;
UPDATE olrt_wb.t SET c3 = '' WHERE id BETWEEN 61 AND 80;
-- 7) delete
DELETE FROM olrt_wb.t2 WHERE id BETWEEN 5100 AND 5400;
DELETE FROM olrt_wb.t WHERE MOD(id, 10) = 0 AND id <= 300;
COMMIT;
