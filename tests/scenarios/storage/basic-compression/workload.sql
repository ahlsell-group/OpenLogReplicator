INSERT /*+ APPEND */ INTO olrt_cmp.t SELECT 100 + LEVEL, MOD(LEVEL, 7), 'same text same text' FROM dual CONNECT BY LEVEL <= 200;
COMMIT;
INSERT INTO olrt_cmp.t SELECT 1000 + LEVEL, 1, 'conventional' FROM dual CONNECT BY LEVEL <= 50;
UPDATE olrt_cmp.t SET qty = 99, txt = 'grown row text that is longer' WHERE id BETWEEN 101 AND 120;
DELETE FROM olrt_cmp.t WHERE id BETWEEN 121 AND 130;
COMMIT;
