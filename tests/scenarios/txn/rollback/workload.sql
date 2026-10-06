UPDATE olrt_rb.t SET qty = 1 WHERE id = 1;
COMMIT;
UPDATE olrt_rb.t SET qty = 999 WHERE id = 2;
INSERT INTO olrt_rb.t VALUES (100, 1, 'never', NULL);
DELETE FROM olrt_rb.t WHERE id = 3;
ROLLBACK;
UPDATE olrt_rb.t SET qty = 2 WHERE id = 4;
COMMIT;
