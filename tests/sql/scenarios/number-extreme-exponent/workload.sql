-- smallest positive NUMBER (1e-130), its negative, the largest magnitude (9.99e125)
INSERT INTO rt_numx.t_numx VALUES (1, 1e-130);
INSERT INTO rt_numx.t_numx VALUES (2, -1e-130);
INSERT INTO rt_numx.t_numx VALUES (3, 9.99e125);
-- the rest of the exponent byte 0x80 range, the first value above it, the largest negative
INSERT INTO rt_numx.t_numx VALUES (4, 1.5e-129);
INSERT INTO rt_numx.t_numx VALUES (5, 9.99e-129);
INSERT INTO rt_numx.t_numx VALUES (6, 1e-128);
INSERT INTO rt_numx.t_numx VALUES (7, -9.99e125);
COMMIT;
UPDATE rt_numx.t_numx SET anynum = 1e-130 WHERE id = 100;
UPDATE rt_numx.t_numx SET anynum = anynum * 10 WHERE id = 4;
COMMIT;
