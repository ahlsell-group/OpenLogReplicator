UPDATE rt_sp.t SET qty = 10 WHERE id = 1;
SAVEPOINT a;
UPDATE rt_sp.t SET qty = 666 WHERE id = 2;
INSERT INTO rt_sp.t VALUES (200, 6, 'phantom', NULL);
DELETE FROM rt_sp.t WHERE id = 3;
ROLLBACK TO SAVEPOINT a;
UPDATE rt_sp.t SET qty = 11 WHERE id = 4;
SAVEPOINT b;
UPDATE rt_sp.t SET txt = 'gone' WHERE id = 5;
-- nested: rolled back to b, then the same row changed again and kept
ROLLBACK TO SAVEPOINT b;
UPDATE rt_sp.t SET txt = 'kept' WHERE id = 5;
SAVEPOINT c;
INSERT INTO rt_sp.t VALUES (201, 7, 'inserted and rolled back', NULL);
UPDATE rt_sp.t SET qty = 12 WHERE id = 6;
ROLLBACK TO SAVEPOINT c;
COMMIT;
-- a transaction rolled back as a whole must leave no trace
UPDATE rt_sp.t SET qty = 777 WHERE id = 7;
INSERT INTO rt_sp.t VALUES (202, 8, 'never committed', NULL);
ROLLBACK;
