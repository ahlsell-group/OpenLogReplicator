-- NUMBER(15,3) holding zero: Oracle stores 0.000 and 0 identically, the value must stay 0
INSERT INTO rt_num.t_num VALUES (1, 0.000, 0, 0, 0, 0.00, 0);
-- negative values, 38-digit integers, a value with 30 integer and 9 fraction digits
INSERT INTO rt_num.t_num VALUES (2, -0.001, -42.750, -1.5e-20, -99999999999999999999999999999999999999, -999.99, -0.1);
INSERT INTO rt_num.t_num VALUES (3, 999999999999.999, 1200.000, 123456789012345678901234567890.123456789, 12345678901234567890123456789012345678, 999.99, 1e125);
INSERT INTO rt_num.t_num VALUES (4, 42.750, 0.100, 1e-20, 7, 0.01, 3.14159265358979);
INSERT INTO rt_num.t_num VALUES (5, NULL, NULL, NULL, NULL, NULL, NULL);
COMMIT;
-- updates to and from zero, thirds in NUMBER without scale, NULL transitions
UPDATE rt_num.t_num SET qty = 0.000 WHERE id = 100;
UPDATE rt_num.t_num SET qty = qty + 0.001, anynum = 1/3 WHERE id = 1;
UPDATE rt_num.t_num SET qty2 = NULL, anynum = 2/3 WHERE id = 2;
UPDATE rt_num.t_num SET qty = 0.5, qty2 = -0.000 WHERE id = 5;
UPDATE rt_num.t_num SET qty = 0, qty2 = 0.000, small = 0 WHERE id = 101;
COMMIT;
DELETE FROM rt_num.t_num WHERE id = 3;
COMMIT;
