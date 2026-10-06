INSERT INTO olrt_n1.t VALUES (10, 0, 1, 0.000, '');
INSERT INTO olrt_n1.t VALUES (11, NULL, 0, 0, ' ');
UPDATE olrt_n1.t SET f = NULL WHERE id = 1;
UPDATE olrt_n1.t SET f = 0 WHERE id = 3;
UPDATE olrt_n1.t SET f = 0 WHERE id = 2;
UPDATE olrt_n1.t SET g = 1 - g, q = NULL WHERE id IN (1, 2);
UPDATE olrt_n1.t SET q = 0 WHERE q IS NULL;
UPDATE olrt_n1.t SET f = NULL, q = NULL, v = NULL WHERE f IS NULL;
COMMIT;
