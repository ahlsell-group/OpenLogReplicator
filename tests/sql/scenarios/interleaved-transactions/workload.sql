-- Three sessions with overlapping transactions; commit order is T2, T1, T3 (begin order T1, T2, T3).
-- Events must come out transaction by transaction in commit order, not interleaved.
-- @session 1
UPDATE rt_il.t SET qty = 100 WHERE id = 1;
-- @session 2
UPDATE rt_il.t SET qty = 200 WHERE id = 2;
-- @session 1
INSERT INTO rt_il.t VALUES (101, 1, 'T1', NULL);
-- @session 3
DELETE FROM rt_il.t WHERE id = 3;
-- @session 2
INSERT INTO rt_il.t VALUES (201, 2, 'T2', NULL);
COMMIT;
-- @session 1
UPDATE rt_il.t SET qty = 101 WHERE id = 1;
COMMIT;
-- @session 2
UPDATE rt_il.t SET qty = 201 WHERE id = 2;
-- @session 3
UPDATE rt_il.t SET txt = 'T3' WHERE id = 4;
COMMIT;
-- @session 2
UPDATE rt_il.t SET qty = 202 WHERE id = 2;
COMMIT;
