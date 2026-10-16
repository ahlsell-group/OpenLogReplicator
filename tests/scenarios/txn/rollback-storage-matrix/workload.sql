-- migrate: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one1' WHERE id = 11;
SAVEPOINT sp1;
UPDATE olrt_rb.mig SET a = RPAD('x', 1800, 'y'), b = RPAD('x', 1800, 'y') WHERE id BETWEEN 15 AND 19;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5003, 3, 'new');
COMMIT;
-- unmigrate: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one4' WHERE id = 32;
SAVEPOINT sp1;
UPDATE olrt_rb.mig SET a = NULL, b = NULL, n = n + 1 WHERE id BETWEEN 36 AND 40;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5006, 6, 'new');
COMMIT;
-- migdel: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one7' WHERE id = 53;
SAVEPOINT sp1;
DELETE FROM olrt_rb.mig WHERE id = 62;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5009, 9, 'new');
COMMIT;
-- wideupd: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one10' WHERE id = 74;
SAVEPOINT sp1;
UPDATE olrt_rb.wide SET c2 = RPAD('w2', 28, 'z'), c9 = RPAD('w9', 28, 'z'), c16 = RPAD('w16', 28, 'z'), c23 = RPAD('w23', 28, 'z'), c30 = RPAD('w30', 28, 'z'), c37 = RPAD('w37', 28, 'z'), c44 = RPAD('w44', 28, 'z'), c51 = RPAD('w51', 28, 'z'), c58 = RPAD('w58', 28, 'z'), c65 = RPAD('w65', 28, 'z'), c72 = RPAD('w72', 28, 'z'), c79 = RPAD('w79', 28, 'z'), c86 = RPAD('w86', 28, 'z'), c93 = RPAD('w93', 28, 'z'), c100 = RPAD('w100', 28, 'z'), c107 = RPAD('w107', 28, 'z'), c114 = RPAD('w114', 28, 'z'), c121 = RPAD('w121', 28, 'z'), c128 = RPAD('w128', 28, 'z'), c135 = RPAD('w135', 28, 'z'), c142 = RPAD('w142', 28, 'z'), c149 = RPAD('w149', 28, 'z'), c156 = RPAD('w156', 28, 'z'), c163 = RPAD('w163', 28, 'z'), c170 = RPAD('w170', 28, 'z'), c177 = RPAD('w177', 28, 'z'), c184 = RPAD('w184', 28, 'z'), c191 = RPAD('w191', 28, 'z'), c198 = RPAD('w198', 28, 'z'), c205 = RPAD('w205', 28, 'z'), c212 = RPAD('w212', 28, 'z'), c219 = RPAD('w219', 28, 'z'), c226 = RPAD('w226', 28, 'z'), c233 = RPAD('w233', 28, 'z'), c240 = RPAD('w240', 28, 'z'), c247 = RPAD('w247', 28, 'z'), c254 = RPAD('w254', 28, 'z'), c261 = RPAD('w261', 28, 'z'), c268 = RPAD('w268', 28, 'z'), c275 = RPAD('w275', 28, 'z'), c282 = RPAD('w282', 28, 'z'), c289 = RPAD('w289', 28, 'z'), c296 = RPAD('w296', 28, 'z') WHERE id BETWEEN 78 AND 80;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5012, 12, 'new');
COMMIT;
-- wideone: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one13' WHERE id = 95;
SAVEPOINT sp1;
UPDATE olrt_rb.wide SET c299 = 'one14' WHERE id = 102;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5015, 15, 'new');
COMMIT;
-- wideins: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one16' WHERE id = 116;
SAVEPOINT sp1;
INSERT INTO olrt_rb.wide (id, c2, c255, c256, c257, c300) VALUES (5017, 'n', 'x255', 'x256', 'x257', 'x300');
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5018, 18, 'new');
COMMIT;
-- widedel: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one19' WHERE id = 137;
SAVEPOINT sp1;
DELETE FROM olrt_rb.wide WHERE id = 145;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5021, 21, 'new');
COMMIT;
-- rowmove: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one22' WHERE id = 158;
SAVEPOINT sp1;
UPDATE olrt_rb.part SET region = MOD(region + 10, 30) WHERE id BETWEEN 162 AND 165;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5024, 24, 'new');
COMMIT;
-- partins: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one25' WHERE id = 179;
SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5026, 26, 'new');
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5027, 27, 'new');
COMMIT;
-- iot: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one28' WHERE id = 20;
SAVEPOINT sp1;
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 24 AND 27;
DELETE FROM olrt_rb.iot WHERE id = 28;
INSERT INTO olrt_rb.iot VALUES (5029, 'ins', 1);
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5030, 0, 'new');
COMMIT;
-- lob: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one31' WHERE id = 41;
SAVEPOINT sp1;
UPDATE olrt_rb.lobt SET c = 'changed ' || id, txt = 'u' WHERE id BETWEEN 46 AND 47;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5033, 3, 'new');
COMMIT;
-- basiccomp: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one34' WHERE id = 62;
SAVEPOINT sp1;
UPDATE olrt_rb.bc SET txt = 'decompressed 35', n = n + 1 WHERE id BETWEEN 66 AND 71;
DELETE FROM olrt_rb.bc WHERE id = 72;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5036, 6, 'new');
COMMIT;
-- multirow: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one37' WHERE id = 83;
SAVEPOINT sp1;
INSERT ALL INTO olrt_rb.part VALUES (20038, 1, 'all') INTO olrt_rb.part VALUES (30038, 15, 'all') INTO olrt_rb.mig VALUES (20038, 'all', NULL, 1) SELECT * FROM dual;
INSERT INTO olrt_rb.iot SELECT 103800 + LEVEL, 'sel', LEVEL FROM dual CONNECT BY LEVEL <= 20;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5039, 9, 'new');
COMMIT;
-- merge: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one40' WHERE id = 104;
SAVEPOINT sp1;
MERGE INTO olrt_rb.mig m USING (SELECT 108 + LEVEL - 1 AS id FROM dual CONNECT BY LEVEL <= 6 UNION ALL SELECT 40041 FROM dual) s ON (m.id = s.id) WHEN MATCHED THEN UPDATE SET m.n = m.n * 2 WHEN NOT MATCHED THEN INSERT (id, n) VALUES (s.id, 0);
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5042, 12, 'new');
COMMIT;
-- cascade: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one43' WHERE id = 125;
SAVEPOINT sp1;
DELETE FROM olrt_rb.parent WHERE id = 30;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5045, 15, 'new');
COMMIT;
-- trigger: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one46' WHERE id = 146;
SAVEPOINT sp1;
UPDATE olrt_rb.parent SET txt = 'tr47' WHERE id = 26;
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5048, 18, 'new');
COMMIT;
-- forall: savepoint rollback
UPDATE olrt_rb.wide SET c299 = 'one49' WHERE id = 167;
SAVEPOINT sp1;
DECLARE TYPE t IS TABLE OF NUMBER; ids t := t(171, 172, 173); BEGIN FORALL i IN 1..ids.COUNT UPDATE olrt_rb.wide SET c100 = 'fa', c280 = 'fa' WHERE id = ids(i); END;
/
ROLLBACK TO SAVEPOINT sp1;
INSERT INTO olrt_rb.part VALUES (5051, 21, 'new');
COMMIT;
-- migrate: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 5 AND 8;
DELETE FROM olrt_rb.iot WHERE id = 9;
INSERT INTO olrt_rb.iot VALUES (5052, 'ins', 1);
UPDATE olrt_rb.mig SET a = RPAD('x', 1800, 'y'), b = RPAD('x', 1800, 'y') WHERE id BETWEEN 12 AND 16;
ROLLBACK;
-- unmigrate: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 19 AND 22;
DELETE FROM olrt_rb.iot WHERE id = 23;
INSERT INTO olrt_rb.iot VALUES (5054, 'ins', 1);
UPDATE olrt_rb.mig SET a = NULL, b = NULL, n = n + 1 WHERE id BETWEEN 26 AND 30;
ROLLBACK;
-- migdel: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 33 AND 36;
DELETE FROM olrt_rb.iot WHERE id = 37;
INSERT INTO olrt_rb.iot VALUES (5056, 'ins', 1);
DELETE FROM olrt_rb.mig WHERE id = 45;
ROLLBACK;
-- wideupd: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 47 AND 50;
DELETE FROM olrt_rb.iot WHERE id = 51;
INSERT INTO olrt_rb.iot VALUES (5058, 'ins', 1);
UPDATE olrt_rb.wide SET c2 = RPAD('w2', 28, 'z'), c9 = RPAD('w9', 28, 'z'), c16 = RPAD('w16', 28, 'z'), c23 = RPAD('w23', 28, 'z'), c30 = RPAD('w30', 28, 'z'), c37 = RPAD('w37', 28, 'z'), c44 = RPAD('w44', 28, 'z'), c51 = RPAD('w51', 28, 'z'), c58 = RPAD('w58', 28, 'z'), c65 = RPAD('w65', 28, 'z'), c72 = RPAD('w72', 28, 'z'), c79 = RPAD('w79', 28, 'z'), c86 = RPAD('w86', 28, 'z'), c93 = RPAD('w93', 28, 'z'), c100 = RPAD('w100', 28, 'z'), c107 = RPAD('w107', 28, 'z'), c114 = RPAD('w114', 28, 'z'), c121 = RPAD('w121', 28, 'z'), c128 = RPAD('w128', 28, 'z'), c135 = RPAD('w135', 28, 'z'), c142 = RPAD('w142', 28, 'z'), c149 = RPAD('w149', 28, 'z'), c156 = RPAD('w156', 28, 'z'), c163 = RPAD('w163', 28, 'z'), c170 = RPAD('w170', 28, 'z'), c177 = RPAD('w177', 28, 'z'), c184 = RPAD('w184', 28, 'z'), c191 = RPAD('w191', 28, 'z'), c198 = RPAD('w198', 28, 'z'), c205 = RPAD('w205', 28, 'z'), c212 = RPAD('w212', 28, 'z'), c219 = RPAD('w219', 28, 'z'), c226 = RPAD('w226', 28, 'z'), c233 = RPAD('w233', 28, 'z'), c240 = RPAD('w240', 28, 'z'), c247 = RPAD('w247', 28, 'z'), c254 = RPAD('w254', 28, 'z'), c261 = RPAD('w261', 28, 'z'), c268 = RPAD('w268', 28, 'z'), c275 = RPAD('w275', 28, 'z'), c282 = RPAD('w282', 28, 'z'), c289 = RPAD('w289', 28, 'z'), c296 = RPAD('w296', 28, 'z') WHERE id BETWEEN 54 AND 56;
ROLLBACK;
-- wideone: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 61 AND 64;
DELETE FROM olrt_rb.iot WHERE id = 65;
INSERT INTO olrt_rb.iot VALUES (5060, 'ins', 1);
UPDATE olrt_rb.wide SET c299 = 'one61' WHERE id = 71;
ROLLBACK;
-- wideins: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 75 AND 78;
DELETE FROM olrt_rb.iot WHERE id = 79;
INSERT INTO olrt_rb.iot VALUES (5062, 'ins', 1);
INSERT INTO olrt_rb.wide (id, c2, c255, c256, c257, c300) VALUES (5063, 'n', 'x255', 'x256', 'x257', 'x300');
ROLLBACK;
-- widedel: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 89 AND 92;
DELETE FROM olrt_rb.iot WHERE id = 93;
INSERT INTO olrt_rb.iot VALUES (5064, 'ins', 1);
DELETE FROM olrt_rb.wide WHERE id = 100;
ROLLBACK;
-- rowmove: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 103 AND 106;
DELETE FROM olrt_rb.iot WHERE id = 107;
INSERT INTO olrt_rb.iot VALUES (5066, 'ins', 1);
UPDATE olrt_rb.part SET region = MOD(region + 10, 30) WHERE id BETWEEN 110 AND 113;
ROLLBACK;
-- partins: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 117 AND 120;
DELETE FROM olrt_rb.iot WHERE id = 121;
INSERT INTO olrt_rb.iot VALUES (5068, 'ins', 1);
INSERT INTO olrt_rb.part VALUES (5069, 9, 'new');
ROLLBACK;
-- iot: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 131 AND 134;
DELETE FROM olrt_rb.iot WHERE id = 135;
INSERT INTO olrt_rb.iot VALUES (5070, 'ins', 1);
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 138 AND 141;
DELETE FROM olrt_rb.iot WHERE id = 142;
INSERT INTO olrt_rb.iot VALUES (5071, 'ins', 1);
ROLLBACK;
-- lob: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 145 AND 148;
DELETE FROM olrt_rb.iot WHERE id = 149;
INSERT INTO olrt_rb.iot VALUES (5072, 'ins', 1);
UPDATE olrt_rb.lobt SET c = 'changed ' || id, txt = 'u' WHERE id BETWEEN 63 AND 64;
ROLLBACK;
-- basiccomp: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 159 AND 162;
DELETE FROM olrt_rb.iot WHERE id = 163;
INSERT INTO olrt_rb.iot VALUES (5074, 'ins', 1);
UPDATE olrt_rb.bc SET txt = 'decompressed 75', n = n + 1 WHERE id BETWEEN 166 AND 171;
DELETE FROM olrt_rb.bc WHERE id = 172;
ROLLBACK;
-- multirow: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 173 AND 176;
DELETE FROM olrt_rb.iot WHERE id = 177;
INSERT INTO olrt_rb.iot VALUES (5076, 'ins', 1);
INSERT ALL INTO olrt_rb.part VALUES (20077, 1, 'all') INTO olrt_rb.part VALUES (30077, 15, 'all') INTO olrt_rb.mig VALUES (20077, 'all', NULL, 1) SELECT * FROM dual;
INSERT INTO olrt_rb.iot SELECT 107700 + LEVEL, 'sel', LEVEL FROM dual CONNECT BY LEVEL <= 20;
ROLLBACK;
-- merge: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 7 AND 10;
DELETE FROM olrt_rb.iot WHERE id = 11;
INSERT INTO olrt_rb.iot VALUES (5078, 'ins', 1);
MERGE INTO olrt_rb.mig m USING (SELECT 14 + LEVEL - 1 AS id FROM dual CONNECT BY LEVEL <= 6 UNION ALL SELECT 40079 FROM dual) s ON (m.id = s.id) WHEN MATCHED THEN UPDATE SET m.n = m.n * 2 WHEN NOT MATCHED THEN INSERT (id, n) VALUES (s.id, 0);
ROLLBACK;
-- cascade: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 21 AND 24;
DELETE FROM olrt_rb.iot WHERE id = 25;
INSERT INTO olrt_rb.iot VALUES (5080, 'ins', 1);
DELETE FROM olrt_rb.parent WHERE id = 29;
ROLLBACK;
-- trigger: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 35 AND 38;
DELETE FROM olrt_rb.iot WHERE id = 39;
INSERT INTO olrt_rb.iot VALUES (5082, 'ins', 1);
UPDATE olrt_rb.parent SET txt = 'tr83' WHERE id = 18;
ROLLBACK;
-- forall: full rollback
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 49 AND 52;
DELETE FROM olrt_rb.iot WHERE id = 53;
INSERT INTO olrt_rb.iot VALUES (5084, 'ins', 1);
DECLARE TYPE t IS TABLE OF NUMBER; ids t := t(56, 57, 58); BEGIN FORALL i IN 1..ids.COUNT UPDATE olrt_rb.wide SET c100 = 'fa', c280 = 'fa' WHERE id = ids(i); END;
/
ROLLBACK;
-- nested savepoints over all shapes
UPDATE olrt_rb.wide SET c299 = 'one86' WHERE id = 66;
SAVEPOINT a;
UPDATE olrt_rb.mig SET a = RPAD('x', 1800, 'y'), b = RPAD('x', 1800, 'y') WHERE id BETWEEN 70 AND 74;
SAVEPOINT s0;
UPDATE olrt_rb.mig SET a = NULL, b = NULL, n = n + 1 WHERE id BETWEEN 77 AND 81;
SAVEPOINT s1;
DELETE FROM olrt_rb.mig WHERE id = 89;
SAVEPOINT s2;
UPDATE olrt_rb.wide SET c2 = RPAD('w2', 28, 'z'), c9 = RPAD('w9', 28, 'z'), c16 = RPAD('w16', 28, 'z'), c23 = RPAD('w23', 28, 'z'), c30 = RPAD('w30', 28, 'z'), c37 = RPAD('w37', 28, 'z'), c44 = RPAD('w44', 28, 'z'), c51 = RPAD('w51', 28, 'z'), c58 = RPAD('w58', 28, 'z'), c65 = RPAD('w65', 28, 'z'), c72 = RPAD('w72', 28, 'z'), c79 = RPAD('w79', 28, 'z'), c86 = RPAD('w86', 28, 'z'), c93 = RPAD('w93', 28, 'z'), c100 = RPAD('w100', 28, 'z'), c107 = RPAD('w107', 28, 'z'), c114 = RPAD('w114', 28, 'z'), c121 = RPAD('w121', 28, 'z'), c128 = RPAD('w128', 28, 'z'), c135 = RPAD('w135', 28, 'z'), c142 = RPAD('w142', 28, 'z'), c149 = RPAD('w149', 28, 'z'), c156 = RPAD('w156', 28, 'z'), c163 = RPAD('w163', 28, 'z'), c170 = RPAD('w170', 28, 'z'), c177 = RPAD('w177', 28, 'z'), c184 = RPAD('w184', 28, 'z'), c191 = RPAD('w191', 28, 'z'), c198 = RPAD('w198', 28, 'z'), c205 = RPAD('w205', 28, 'z'), c212 = RPAD('w212', 28, 'z'), c219 = RPAD('w219', 28, 'z'), c226 = RPAD('w226', 28, 'z'), c233 = RPAD('w233', 28, 'z'), c240 = RPAD('w240', 28, 'z'), c247 = RPAD('w247', 28, 'z'), c254 = RPAD('w254', 28, 'z'), c261 = RPAD('w261', 28, 'z'), c268 = RPAD('w268', 28, 'z'), c275 = RPAD('w275', 28, 'z'), c282 = RPAD('w282', 28, 'z'), c289 = RPAD('w289', 28, 'z'), c296 = RPAD('w296', 28, 'z') WHERE id BETWEEN 91 AND 93;
SAVEPOINT s3;
UPDATE olrt_rb.wide SET c299 = 'one91' WHERE id = 101;
SAVEPOINT s4;
INSERT INTO olrt_rb.wide (id, c2, c255, c256, c257, c300) VALUES (5092, 'n', 'x255', 'x256', 'x257', 'x300');
SAVEPOINT s5;
DELETE FROM olrt_rb.wide WHERE id = 116;
SAVEPOINT s6;
UPDATE olrt_rb.part SET region = MOD(region + 10, 30) WHERE id BETWEEN 119 AND 122;
SAVEPOINT s7;
INSERT INTO olrt_rb.part VALUES (5095, 5, 'new');
SAVEPOINT s8;
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 133 AND 136;
DELETE FROM olrt_rb.iot WHERE id = 137;
INSERT INTO olrt_rb.iot VALUES (5096, 'ins', 1);
SAVEPOINT s9;
UPDATE olrt_rb.lobt SET c = 'changed ' || id, txt = 'u' WHERE id BETWEEN 51 AND 52;
SAVEPOINT s10;
UPDATE olrt_rb.bc SET txt = 'decompressed 98', n = n + 1 WHERE id BETWEEN 147 AND 152;
DELETE FROM olrt_rb.bc WHERE id = 153;
SAVEPOINT s11;
INSERT ALL INTO olrt_rb.part VALUES (20099, 1, 'all') INTO olrt_rb.part VALUES (30099, 15, 'all') INTO olrt_rb.mig VALUES (20099, 'all', NULL, 1) SELECT * FROM dual;
INSERT INTO olrt_rb.iot SELECT 109900 + LEVEL, 'sel', LEVEL FROM dual CONNECT BY LEVEL <= 20;
SAVEPOINT s12;
MERGE INTO olrt_rb.mig m USING (SELECT 161 + LEVEL - 1 AS id FROM dual CONNECT BY LEVEL <= 6 UNION ALL SELECT 40100 FROM dual) s ON (m.id = s.id) WHEN MATCHED THEN UPDATE SET m.n = m.n * 2 WHEN NOT MATCHED THEN INSERT (id, n) VALUES (s.id, 0);
SAVEPOINT s13;
DELETE FROM olrt_rb.parent WHERE id = 19;
SAVEPOINT s14;
UPDATE olrt_rb.parent SET txt = 'tr102' WHERE id = 1;
SAVEPOINT s15;
DECLARE TYPE t IS TABLE OF NUMBER; ids t := t(2, 3, 4); BEGIN FORALL i IN 1..ids.COUNT UPDATE olrt_rb.wide SET c100 = 'fa', c280 = 'fa' WHERE id = ids(i); END;
/
SAVEPOINT s16;
ROLLBACK TO SAVEPOINT s3;
ROLLBACK TO SAVEPOINT a;
UPDATE olrt_rb.mig SET a = RPAD('x', 1800, 'y'), b = RPAD('x', 1800, 'y') WHERE id BETWEEN 9 AND 13;
COMMIT;
-- savepoint rollback across a log switch
UPDATE olrt_rb.wide SET c2 = RPAD('w2', 28, 'z'), c9 = RPAD('w9', 28, 'z'), c16 = RPAD('w16', 28, 'z'), c23 = RPAD('w23', 28, 'z'), c30 = RPAD('w30', 28, 'z'), c37 = RPAD('w37', 28, 'z'), c44 = RPAD('w44', 28, 'z'), c51 = RPAD('w51', 28, 'z'), c58 = RPAD('w58', 28, 'z'), c65 = RPAD('w65', 28, 'z'), c72 = RPAD('w72', 28, 'z'), c79 = RPAD('w79', 28, 'z'), c86 = RPAD('w86', 28, 'z'), c93 = RPAD('w93', 28, 'z'), c100 = RPAD('w100', 28, 'z'), c107 = RPAD('w107', 28, 'z'), c114 = RPAD('w114', 28, 'z'), c121 = RPAD('w121', 28, 'z'), c128 = RPAD('w128', 28, 'z'), c135 = RPAD('w135', 28, 'z'), c142 = RPAD('w142', 28, 'z'), c149 = RPAD('w149', 28, 'z'), c156 = RPAD('w156', 28, 'z'), c163 = RPAD('w163', 28, 'z'), c170 = RPAD('w170', 28, 'z'), c177 = RPAD('w177', 28, 'z'), c184 = RPAD('w184', 28, 'z'), c191 = RPAD('w191', 28, 'z'), c198 = RPAD('w198', 28, 'z'), c205 = RPAD('w205', 28, 'z'), c212 = RPAD('w212', 28, 'z'), c219 = RPAD('w219', 28, 'z'), c226 = RPAD('w226', 28, 'z'), c233 = RPAD('w233', 28, 'z'), c240 = RPAD('w240', 28, 'z'), c247 = RPAD('w247', 28, 'z'), c254 = RPAD('w254', 28, 'z'), c261 = RPAD('w261', 28, 'z'), c268 = RPAD('w268', 28, 'z'), c275 = RPAD('w275', 28, 'z'), c282 = RPAD('w282', 28, 'z'), c289 = RPAD('w289', 28, 'z'), c296 = RPAD('w296', 28, 'z') WHERE id BETWEEN 16 AND 18;
SAVEPOINT x;
UPDATE olrt_rb.mig SET a = RPAD('x', 1800, 'y'), b = RPAD('x', 1800, 'y') WHERE id BETWEEN 23 AND 27;
UPDATE olrt_rb.part SET region = MOD(region + 10, 30) WHERE id BETWEEN 30 AND 33;
UPDATE olrt_rb.wide SET c2 = RPAD('w2', 28, 'z'), c9 = RPAD('w9', 28, 'z'), c16 = RPAD('w16', 28, 'z'), c23 = RPAD('w23', 28, 'z'), c30 = RPAD('w30', 28, 'z'), c37 = RPAD('w37', 28, 'z'), c44 = RPAD('w44', 28, 'z'), c51 = RPAD('w51', 28, 'z'), c58 = RPAD('w58', 28, 'z'), c65 = RPAD('w65', 28, 'z'), c72 = RPAD('w72', 28, 'z'), c79 = RPAD('w79', 28, 'z'), c86 = RPAD('w86', 28, 'z'), c93 = RPAD('w93', 28, 'z'), c100 = RPAD('w100', 28, 'z'), c107 = RPAD('w107', 28, 'z'), c114 = RPAD('w114', 28, 'z'), c121 = RPAD('w121', 28, 'z'), c128 = RPAD('w128', 28, 'z'), c135 = RPAD('w135', 28, 'z'), c142 = RPAD('w142', 28, 'z'), c149 = RPAD('w149', 28, 'z'), c156 = RPAD('w156', 28, 'z'), c163 = RPAD('w163', 28, 'z'), c170 = RPAD('w170', 28, 'z'), c177 = RPAD('w177', 28, 'z'), c184 = RPAD('w184', 28, 'z'), c191 = RPAD('w191', 28, 'z'), c198 = RPAD('w198', 28, 'z'), c205 = RPAD('w205', 28, 'z'), c212 = RPAD('w212', 28, 'z'), c219 = RPAD('w219', 28, 'z'), c226 = RPAD('w226', 28, 'z'), c233 = RPAD('w233', 28, 'z'), c240 = RPAD('w240', 28, 'z'), c247 = RPAD('w247', 28, 'z'), c254 = RPAD('w254', 28, 'z'), c261 = RPAD('w261', 28, 'z'), c268 = RPAD('w268', 28, 'z'), c275 = RPAD('w275', 28, 'z'), c282 = RPAD('w282', 28, 'z'), c289 = RPAD('w289', 28, 'z'), c296 = RPAD('w296', 28, 'z') WHERE id BETWEEN 37 AND 39;
-- @switch_logfile
UPDATE olrt_rb.lobt SET c = 'changed ' || id, txt = 'u' WHERE id BETWEEN 45 AND 46;
UPDATE olrt_rb.iot SET txt = RPAD('iot', 90, 'q'), n = -n WHERE id BETWEEN 51 AND 54;
DELETE FROM olrt_rb.iot WHERE id = 55;
INSERT INTO olrt_rb.iot VALUES (5110, 'ins', 1);
ROLLBACK TO SAVEPOINT x;
UPDATE olrt_rb.mig SET a = RPAD('x', 1800, 'y'), b = RPAD('x', 1800, 'y') WHERE id BETWEEN 58 AND 62;
COMMIT;
-- same rows before and inside the savepoint
UPDATE olrt_rb.mig SET a = 'first' WHERE id = 300;
UPDATE olrt_rb.wide SET c290 = 'first' WHERE id = 150;
SAVEPOINT y;
UPDATE olrt_rb.mig SET a = RPAD('x', 1800, 'y') WHERE id = 300;
UPDATE olrt_rb.wide SET c2 = RPAD('w2', 28, 'z'), c9 = RPAD('w9', 28, 'z'), c16 = RPAD('w16', 28, 'z'), c23 = RPAD('w23', 28, 'z'), c30 = RPAD('w30', 28, 'z'), c37 = RPAD('w37', 28, 'z'), c44 = RPAD('w44', 28, 'z'), c51 = RPAD('w51', 28, 'z'), c58 = RPAD('w58', 28, 'z'), c65 = RPAD('w65', 28, 'z'), c72 = RPAD('w72', 28, 'z'), c79 = RPAD('w79', 28, 'z'), c86 = RPAD('w86', 28, 'z'), c93 = RPAD('w93', 28, 'z'), c100 = RPAD('w100', 28, 'z'), c107 = RPAD('w107', 28, 'z'), c114 = RPAD('w114', 28, 'z'), c121 = RPAD('w121', 28, 'z'), c128 = RPAD('w128', 28, 'z'), c135 = RPAD('w135', 28, 'z'), c142 = RPAD('w142', 28, 'z'), c149 = RPAD('w149', 28, 'z'), c156 = RPAD('w156', 28, 'z'), c163 = RPAD('w163', 28, 'z'), c170 = RPAD('w170', 28, 'z'), c177 = RPAD('w177', 28, 'z'), c184 = RPAD('w184', 28, 'z'), c191 = RPAD('w191', 28, 'z'), c198 = RPAD('w198', 28, 'z'), c205 = RPAD('w205', 28, 'z'), c212 = RPAD('w212', 28, 'z'), c219 = RPAD('w219', 28, 'z'), c226 = RPAD('w226', 28, 'z'), c233 = RPAD('w233', 28, 'z'), c240 = RPAD('w240', 28, 'z'), c247 = RPAD('w247', 28, 'z'), c254 = RPAD('w254', 28, 'z'), c261 = RPAD('w261', 28, 'z'), c268 = RPAD('w268', 28, 'z'), c275 = RPAD('w275', 28, 'z'), c282 = RPAD('w282', 28, 'z'), c289 = RPAD('w289', 28, 'z'), c296 = RPAD('w296', 28, 'z') WHERE id = 150;
DELETE FROM olrt_rb.wide WHERE id = 150;
ROLLBACK TO SAVEPOINT y;
UPDATE olrt_rb.mig SET b = 'after' WHERE id = 300;
COMMIT;
-- interleaved sessions
-- @session 1
UPDATE olrt_rb.wide SET c2 = RPAD('w2', 28, 'z'), c9 = RPAD('w9', 28, 'z'), c16 = RPAD('w16', 28, 'z'), c23 = RPAD('w23', 28, 'z'), c30 = RPAD('w30', 28, 'z'), c37 = RPAD('w37', 28, 'z'), c44 = RPAD('w44', 28, 'z'), c51 = RPAD('w51', 28, 'z'), c58 = RPAD('w58', 28, 'z'), c65 = RPAD('w65', 28, 'z'), c72 = RPAD('w72', 28, 'z'), c79 = RPAD('w79', 28, 'z'), c86 = RPAD('w86', 28, 'z'), c93 = RPAD('w93', 28, 'z'), c100 = RPAD('w100', 28, 'z'), c107 = RPAD('w107', 28, 'z'), c114 = RPAD('w114', 28, 'z'), c121 = RPAD('w121', 28, 'z'), c128 = RPAD('w128', 28, 'z'), c135 = RPAD('w135', 28, 'z'), c142 = RPAD('w142', 28, 'z'), c149 = RPAD('w149', 28, 'z'), c156 = RPAD('w156', 28, 'z'), c163 = RPAD('w163', 28, 'z'), c170 = RPAD('w170', 28, 'z'), c177 = RPAD('w177', 28, 'z'), c184 = RPAD('w184', 28, 'z'), c191 = RPAD('w191', 28, 'z'), c198 = RPAD('w198', 28, 'z'), c205 = RPAD('w205', 28, 'z'), c212 = RPAD('w212', 28, 'z'), c219 = RPAD('w219', 28, 'z'), c226 = RPAD('w226', 28, 'z'), c233 = RPAD('w233', 28, 'z'), c240 = RPAD('w240', 28, 'z'), c247 = RPAD('w247', 28, 'z'), c254 = RPAD('w254', 28, 'z'), c261 = RPAD('w261', 28, 'z'), c268 = RPAD('w268', 28, 'z'), c275 = RPAD('w275', 28, 'z'), c282 = RPAD('w282', 28, 'z'), c289 = RPAD('w289', 28, 'z'), c296 = RPAD('w296', 28, 'z') WHERE id BETWEEN 65 AND 67;
SAVEPOINT p;
-- @session 2
UPDATE olrt_rb.mig SET a = RPAD('x', 1800, 'y'), b = RPAD('x', 1800, 'y') WHERE id BETWEEN 72 AND 76;
SAVEPOINT q;
-- @session 1
UPDATE olrt_rb.part SET region = MOD(region + 10, 30) WHERE id BETWEEN 79 AND 82;
DELETE FROM olrt_rb.wide WHERE id = 90;
-- @session 2
UPDATE olrt_rb.mig SET a = NULL, b = NULL, n = n + 1 WHERE id BETWEEN 93 AND 97;
ROLLBACK TO SAVEPOINT q;
-- @session 1
ROLLBACK TO SAVEPOINT p;
COMMIT;
-- @session 2
COMMIT;
-- @session 1
-- failing multi-row statement on wide rows, then a savepoint rollback
SAVEPOINT z;
-- @expect_error ORA-00001
INSERT INTO olrt_rb.wide (id, c2) SELECT 9000 + LEVEL - 1 - DECODE(LEVEL, 30, 1, 0), 'dup' FROM dual CONNECT BY LEVEL <= 30;
UPDATE olrt_rb.wide SET c2 = RPAD('w2', 28, 'z'), c9 = RPAD('w9', 28, 'z'), c16 = RPAD('w16', 28, 'z'), c23 = RPAD('w23', 28, 'z'), c30 = RPAD('w30', 28, 'z'), c37 = RPAD('w37', 28, 'z'), c44 = RPAD('w44', 28, 'z'), c51 = RPAD('w51', 28, 'z'), c58 = RPAD('w58', 28, 'z'), c65 = RPAD('w65', 28, 'z'), c72 = RPAD('w72', 28, 'z'), c79 = RPAD('w79', 28, 'z'), c86 = RPAD('w86', 28, 'z'), c93 = RPAD('w93', 28, 'z'), c100 = RPAD('w100', 28, 'z'), c107 = RPAD('w107', 28, 'z'), c114 = RPAD('w114', 28, 'z'), c121 = RPAD('w121', 28, 'z'), c128 = RPAD('w128', 28, 'z'), c135 = RPAD('w135', 28, 'z'), c142 = RPAD('w142', 28, 'z'), c149 = RPAD('w149', 28, 'z'), c156 = RPAD('w156', 28, 'z'), c163 = RPAD('w163', 28, 'z'), c170 = RPAD('w170', 28, 'z'), c177 = RPAD('w177', 28, 'z'), c184 = RPAD('w184', 28, 'z'), c191 = RPAD('w191', 28, 'z'), c198 = RPAD('w198', 28, 'z'), c205 = RPAD('w205', 28, 'z'), c212 = RPAD('w212', 28, 'z'), c219 = RPAD('w219', 28, 'z'), c226 = RPAD('w226', 28, 'z'), c233 = RPAD('w233', 28, 'z'), c240 = RPAD('w240', 28, 'z'), c247 = RPAD('w247', 28, 'z'), c254 = RPAD('w254', 28, 'z'), c261 = RPAD('w261', 28, 'z'), c268 = RPAD('w268', 28, 'z'), c275 = RPAD('w275', 28, 'z'), c282 = RPAD('w282', 28, 'z'), c289 = RPAD('w289', 28, 'z'), c296 = RPAD('w296', 28, 'z') WHERE id BETWEEN 100 AND 102;
ROLLBACK TO SAVEPOINT z;
UPDATE olrt_rb.wide SET c299 = 'one118' WHERE id = 110;
COMMIT;
