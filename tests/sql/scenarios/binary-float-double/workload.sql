-- typed literals (f/d): an untyped literal is a NUMBER and cannot hold a subnormal double
INSERT INTO rt_bin.t_bin VALUES (1, 1f, 1d);
INSERT INTO rt_bin.t_bin VALUES (2, 1.5f, -2.25d);
INSERT INTO rt_bin.t_bin VALUES (3, 16777216f, 9007199254740992d);
INSERT INTO rt_bin.t_bin VALUES (4, 1.0e-40f, 4.9e-324d);
INSERT INTO rt_bin.t_bin VALUES (5, -1.0e-40f, -2.2e-308d);
INSERT INTO rt_bin.t_bin VALUES (6, 3.4028235e38f, 1.7976931348623157e308d);
INSERT INTO rt_bin.t_bin VALUES (7, 0.1f, 0.30000000000000004d);
INSERT INTO rt_bin.t_bin VALUES (8, 0f, 0d);
COMMIT;
