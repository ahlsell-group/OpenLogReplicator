INSERT INTO olrt_chr.t VALUES (1, 'J', 'ab', N'åä', ' ');
INSERT INTO olrt_chr.t VALUES (2, 'N', 'abcdefghij', N'x', NULL);
INSERT INTO olrt_chr.t VALUES (3, NULL, '  lead', NULL, '   ');
COMMIT;
UPDATE olrt_chr.t SET flag = 'N', c10 = 'abc' WHERE id = 1;
UPDATE olrt_chr.t SET c10 = 'a' WHERE id = 2;
COMMIT;
