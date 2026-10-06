MERGE INTO olrt_mr.t d
USING (SELECT LEVEL + 15 id, LEVEL * 7 qty FROM dual CONNECT BY LEVEL <= 10) s
ON (d.id = s.id)
WHEN MATCHED THEN UPDATE SET d.qty = s.qty
WHEN NOT MATCHED THEN INSERT (id, qty, txt) VALUES (s.id, s.qty, 'merged');
INSERT ALL
  INTO olrt_mr.t (id, qty, txt) VALUES (id, 1, 'all-1')
  INTO olrt_mr.t2 (id, qty, txt) VALUES (id, 2, 'all-2')
SELECT 300 + LEVEL id FROM dual CONNECT BY LEVEL <= 5;
INSERT INTO olrt_mr.t2 SELECT id + 1000, qty, txt, d FROM olrt_mr.t WHERE id <= 20;
UPDATE olrt_mr.t SET txt = txt || '!' WHERE MOD(id, 2) = 0;
DELETE FROM olrt_mr.t2 WHERE id > 1010;
COMMIT;
