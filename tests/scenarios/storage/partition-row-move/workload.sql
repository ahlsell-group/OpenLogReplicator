UPDATE olrt_pt.r SET region = region + 10 WHERE id <= 20;
UPDATE olrt_pt.r SET region = 1, txt = 'moved' WHERE id BETWEEN 40 AND 45;
UPDATE olrt_pt.r SET txt = 'same-part' WHERE id BETWEEN 21 AND 25;
DELETE FROM olrt_pt.r WHERE id BETWEEN 30 AND 33;
COMMIT;
-- new interval partitions appear on insert (recursive DDL in the redo)
INSERT INTO olrt_pt.i SELECT 100 + LEVEL, DATE '2027-01-15' + LEVEL * 31, 'new' || LEVEL FROM dual CONNECT BY LEVEL <= 6;
COMMIT;
-- @expect_error ORA-14402
UPDATE olrt_pt.i SET d = d + 90 WHERE id <= 5;
ALTER TABLE olrt_pt.i ENABLE ROW MOVEMENT;
UPDATE olrt_pt.i SET d = d + 90 WHERE id <= 5;
ALTER TABLE olrt_pt.r TRUNCATE PARTITION p1 UPDATE GLOBAL INDEXES;
INSERT INTO olrt_pt.r VALUES (900, 5, 'after-trunc', SYSDATE);
COMMIT;
