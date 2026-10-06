-- one txn: insert, update, update, delete
INSERT INTO olrt_srm.t VALUES (10, 1, 'n', DATE '2026-02-01');
UPDATE olrt_srm.t SET qty = 2 WHERE id = 10;
UPDATE olrt_srm.t SET txt = 'nn', d = NULL WHERE id = 10;
DELETE FROM olrt_srm.t WHERE id = 10;
COMMIT;
-- existing row: update, delete, re-insert same key, update again
UPDATE olrt_srm.t SET qty = 20 WHERE id = 1;
DELETE FROM olrt_srm.t WHERE id = 1;
INSERT INTO olrt_srm.t VALUES (1, 111, 'again', DATE '2027-01-01');
UPDATE olrt_srm.t SET qty = qty + 1 WHERE id = 1;
COMMIT;
-- a second DELETE of the same key is a no-op in Oracle (no redo): exactly one d event is expected
DELETE FROM olrt_srm.t WHERE id = 2;
DELETE FROM olrt_srm.t WHERE id = 2;
UPDATE olrt_srm.t SET txt = 'x' WHERE id = 2;
COMMIT;
-- same row updated 50 times in one txn
BEGIN
  FOR i IN 1 .. 50 LOOP UPDATE olrt_srm.t SET qty = i, txt = 'v' || i WHERE id = 3; END LOOP;
END;
/
COMMIT;
