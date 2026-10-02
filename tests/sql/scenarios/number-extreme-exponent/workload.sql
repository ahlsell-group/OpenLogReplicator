-- smallest positive NUMBER (1e-130), its negative, the largest magnitude (9.99e125)
INSERT INTO rt_numx.t_numx VALUES (1, 1e-130);
INSERT INTO rt_numx.t_numx VALUES (2, -1e-130);
INSERT INTO rt_numx.t_numx VALUES (3, 9.99e125);
COMMIT;
UPDATE rt_numx.t_numx SET anynum = 1e-130 WHERE id = 100;
COMMIT;
