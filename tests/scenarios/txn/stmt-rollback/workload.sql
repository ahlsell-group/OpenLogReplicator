INSERT INTO olrt_srb.t VALUES (100, 1, 'ok');
-- fails on the 4th row (duplicate key), the first three rows are undone by the statement rollback
-- @expect_error ORA-00001
INSERT INTO olrt_srb.t SELECT 200 + LEVEL, 1, 'dup' FROM dual CONNECT BY LEVEL <= 3 UNION ALL SELECT 5, 1, 'dup' FROM dual;
UPDATE olrt_srb.t SET txt = 'upd' WHERE id <= 3;
-- check constraint fails after some rows were already updated
-- @expect_error ORA-02290
UPDATE olrt_srb.t SET qty = CASE WHEN id < 8 THEN qty + 1 ELSE 5000 END WHERE id BETWEEN 4 AND 9;
-- value too large for the column
-- @expect_error ORA-12899
INSERT INTO olrt_srb.t SELECT 300 + LEVEL, 1, CASE WHEN LEVEL = 3 THEN 'this-is-too-long' ELSE 'fine' END FROM dual CONNECT BY LEVEL <= 5;
INSERT INTO olrt_srb.t VALUES (101, 2, 'ok2');
COMMIT;
