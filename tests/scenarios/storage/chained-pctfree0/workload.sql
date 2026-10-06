UPDATE olrt_ch.t SET a = RPAD('a', 300, 'a'), b = RPAD('b', 300, 'b') WHERE MOD(id, 2) = 0;
UPDATE olrt_ch.t SET c = RPAD('c', 390, 'c') WHERE MOD(id, 4) = 0;
UPDATE olrt_ch.t SET n = n + 1 WHERE id <= 500;
UPDATE olrt_ch.t SET a = NULL WHERE MOD(id, 6) = 0;
DELETE FROM olrt_ch.t WHERE MOD(id, 10) = 0;
COMMIT;
UPDATE olrt_ch.t SET a = RPAD('z', 350, 'z') WHERE MOD(id, 3) = 0;
COMMIT;
