SELECT id FROM olrt_lkr.t WHERE id <= 5 FOR UPDATE;
UPDATE olrt_lkr.t SET qty = qty * 2 WHERE id = 1;
UPDATE olrt_lkr.t SET qty = qty WHERE id = 2;
INSERT INTO olrt_lkr.t (id, qty) VALUES (50, 5);
DELETE FROM olrt_lkr.t WHERE id = 3;
COMMIT;
