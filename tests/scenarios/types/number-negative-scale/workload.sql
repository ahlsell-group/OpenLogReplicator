INSERT INTO olrt_negsc.t VALUES (1, 12345, 0);
INSERT INTO olrt_negsc.t VALUES (2, -987654, 1);
UPDATE olrt_negsc.t SET h = 5050 WHERE id = 1;
COMMIT;
