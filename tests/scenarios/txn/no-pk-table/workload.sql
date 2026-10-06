INSERT INTO olrt_nopk.t VALUES ('P1', 3, 30.000, 'N');
UPDATE olrt_nopk.t SET qty = 0.000 WHERE item_no = 'P1' AND pack_no = 1;
DELETE FROM olrt_nopk.t WHERE item_no = 'P1' AND pack_no = 2;
INSERT INTO olrt_nopk.u VALUES (3, 'c');
UPDATE olrt_nopk.u SET v = 'B' WHERE k = 2;
DELETE FROM olrt_nopk.u WHERE k = 1;
COMMIT;
