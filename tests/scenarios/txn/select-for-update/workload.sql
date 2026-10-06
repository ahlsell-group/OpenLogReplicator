SELECT id FROM olrt_sfu.t WHERE id <= 5 FOR UPDATE;

UPDATE olrt_sfu.t SET qty = qty * 2 WHERE id = 1;
UPDATE olrt_sfu.t SET qty = qty WHERE id = 2;
INSERT INTO olrt_sfu.t (id, qty) VALUES (50, 5);
DELETE FROM olrt_sfu.t WHERE id = 3;
COMMIT;
