INSERT INTO olrt_bin.t VALUES (1, 1, 1);
INSERT INTO olrt_bin.t VALUES (2, 1.5, -2.25);
INSERT INTO olrt_bin.t VALUES (3, 16777216, 9007199254740992);
INSERT INTO olrt_bin.t VALUES (4, 1.0e-40, 4.9e-324);
INSERT INTO olrt_bin.t VALUES (5, -0.0, 0.1);
-- typed literals: 4.9e-324 and -0.0 above are NUMBER literals and arrive as 0
INSERT INTO olrt_bin.t VALUES (6, -0.0f, 4.9e-324d);
INSERT INTO olrt_bin.t VALUES (7, -1.0e-40f, -2.2e-308d);
INSERT INTO olrt_bin.t VALUES (8, 3.4028235e38f, 1.7976931348623157e308d);
INSERT INTO olrt_bin.t VALUES (9, 0.1f, 0.30000000000000004d);
INSERT INTO olrt_bin.t VALUES (10, 16777216f, 9007199254740992d);
INSERT INTO olrt_bin.t VALUES (11, 1.0e-40f, 4.9e-324d);
INSERT INTO olrt_bin.t VALUES (12, 0f, 0d);
COMMIT;
