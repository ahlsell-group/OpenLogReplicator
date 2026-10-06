INSERT INTO olrt_c1.t VALUES (100, 0.000, -12.5, 'new', 'x', DATE '9999-12-31', TIMESTAMP '2026-10-25 02:30:00.000001', NULL);
UPDATE olrt_c1.t SET qty = 7.25, txt = 'upd' WHERE id = 1;
COMMIT;
DELETE FROM olrt_c1.t WHERE id = 2;
UPDATE olrt_c1.t SET n = NULL WHERE id = 3;
COMMIT;
INSERT INTO olrt_c1.t (id, qty, txt) VALUES (101, 1, 'only required');
COMMIT;
