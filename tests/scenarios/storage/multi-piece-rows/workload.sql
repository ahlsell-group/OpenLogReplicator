-- change in the first piece only (C2..C256), in the second only (C257..C300), and in both
UPDATE olrt_piece.wide_c SET c5 = 'first piece' WHERE id = 1;
UPDATE olrt_piece.wide_c SET c290 = 'second piece', c300 = DATE '2030-12-31' WHERE id = 2;
UPDATE olrt_piece.wide_c SET c6 = DATE '2031-01-01', c298 = 123456789012.345 WHERE id = 3;
-- value to NULL and back from NULL, in both pieces
UPDATE olrt_piece.wide_c SET c8 = NULL, c297 = NULL WHERE id = 4;
UPDATE olrt_piece.wide_c SET c297 = DATE '2031-02-02' WHERE id = 4;
-- update that sets a column to its current value
UPDATE olrt_piece.wide_c SET c11 = c11, c298 = c298 WHERE id = 5;
-- grow every VARCHAR2 so the row no longer fits in its block and migrates
UPDATE olrt_piece.wide_c SET
    c2 = RPAD('g', 60, 'g'), c5 = RPAD('g', 60, 'g'), c8 = RPAD('g', 60, 'g'), c11 = RPAD('g', 60, 'g'),
    c14 = RPAD('g', 60, 'g'), c17 = RPAD('g', 60, 'g'), c20 = RPAD('g', 60, 'g'), c23 = RPAD('g', 60, 'g'),
    c26 = RPAD('g', 60, 'g'), c29 = RPAD('g', 60, 'g'), c32 = RPAD('g', 60, 'g'), c35 = RPAD('g', 60, 'g'),
    c260 = RPAD('g', 60, 'g'), c263 = RPAD('g', 60, 'g'), c266 = RPAD('g', 60, 'g'), c269 = RPAD('g', 60, 'g'),
    c272 = RPAD('g', 60, 'g'), c275 = RPAD('g', 60, 'g'), c278 = RPAD('g', 60, 'g'), c281 = RPAD('g', 60, 'g')
 WHERE id BETWEEN 10 AND 60;
COMMIT;
DELETE FROM olrt_piece.wide_c WHERE id IN (1, 15, 99);
INSERT INTO olrt_piece.wide_c (id, c2, c3, c4, c299, c300) VALUES (500, 'sparse', DATE '2026-05-05', 1.5, 'tail', DATE '2026-06-06');
UPDATE olrt_piece.wide_c SET c2 = 'moved after migration' WHERE id = 20;
COMMIT;
