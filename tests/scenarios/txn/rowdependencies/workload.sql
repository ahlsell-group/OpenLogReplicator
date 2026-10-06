UPDATE olrt_rdep.t SET qty = qty * 2 WHERE id = 1;
UPDATE olrt_rdep.t SET qty = qty WHERE id = 2;
INSERT INTO olrt_rdep.t (id, qty) VALUES (50, 5);
DELETE FROM olrt_rdep.t WHERE id = 3;
COMMIT;
