INSERT INTO olrt_oc.t SELECT 1000 + LEVEL, 'same text', 1.5, DATE '2026-01-01' FROM dual CONNECT BY LEVEL <= 3000;
UPDATE olrt_oc.t SET a = 'changed', n = n + 1 WHERE MOD(id, 3) = 0;
DELETE FROM olrt_oc.t WHERE MOD(id, 7) = 0;
COMMIT;
