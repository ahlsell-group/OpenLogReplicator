INSERT INTO olrt_txt.t VALUES (1, 'Rörböj 22 mm förzinkad åäöÅÄÖ', N'Ångbäck');
INSERT INTO olrt_txt.t VALUES (2, 'Pris 10 € – „citat“ … ™', N'€');
INSERT INTO olrt_txt.t VALUES (3, '漢字かなカナ 한국어', N'中文');
INSERT INTO olrt_txt.t VALUES (4, 'emoji 😀 👍🏽 and e' || UNISTR('\0301'), N'😀');
COMMIT;
UPDATE olrt_txt.t SET v = v || ' ÆØ' WHERE id = 1;
COMMIT;
