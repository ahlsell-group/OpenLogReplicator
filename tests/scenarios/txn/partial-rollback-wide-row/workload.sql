SELECT id FROM wide.wide_313 WHERE id = 252 FOR UPDATE;
UPDATE wide.wide_313 SET c8 = 'before-sp' WHERE id = 252;
SAVEPOINT sp1;
UPDATE wide.wide_313 SET c302 = 'rolled-back', c5 = 'rolled-back' WHERE id = 252;
DELETE FROM wide.wide_313 WHERE id = 1252;
UPDATE wide.wide_278 SET c270 = DATE '2030-01-01' WHERE id = 252;
ROLLBACK TO SAVEPOINT sp1;
UPDATE wide.wide_313 SET c11 = 'after-sp' WHERE id = 252;
COMMIT;
