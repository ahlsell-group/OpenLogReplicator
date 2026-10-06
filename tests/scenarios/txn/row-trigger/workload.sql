UPDATE olrt_rtrg.t SET qty = qty * 2 WHERE id = 1;
UPDATE olrt_rtrg.t SET qty = qty WHERE id = 2;
INSERT INTO olrt_rtrg.t (id, qty) VALUES (50, 5);
DELETE FROM olrt_rtrg.t WHERE id = 3;
COMMIT;
