SELECT id FROM olrt_sfurd.t WHERE id <= 5 FOR UPDATE;

UPDATE olrt_sfurd.t SET qty = qty * 2 WHERE id = 1;
UPDATE olrt_sfurd.t SET qty = qty WHERE id = 2;
INSERT INTO olrt_sfurd.t (id, qty) VALUES (50, 5);
DELETE FROM olrt_sfurd.t WHERE id = 3;
COMMIT;
