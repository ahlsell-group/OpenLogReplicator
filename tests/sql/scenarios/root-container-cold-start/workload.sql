INSERT INTO c##rt_root.t VALUES (2, 'first');
COMMIT;
INSERT INTO c##rt_root.t VALUES (3, 'kept');
SAVEPOINT sp1;
INSERT INTO c##rt_root.t VALUES (4, 'rolled back');
ROLLBACK TO SAVEPOINT sp1;
COMMIT;
UPDATE c##rt_root.t SET v = 'updated' WHERE id = 1;
COMMIT;
