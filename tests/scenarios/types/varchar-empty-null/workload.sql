INSERT INTO olrt_vc.t VALUES (1, '', ' ', 'x');
INSERT INTO olrt_vc.t VALUES (2, NULL, '  ', NULL);
COMMIT;
UPDATE olrt_vc.t SET b = '', c = NULL WHERE id = 1;
UPDATE olrt_vc.t SET a = 'now set', c = '' WHERE id = 2;
COMMIT;
UPDATE olrt_vc.t SET a = NULL, b = NULL, c = NULL WHERE id = 2;
COMMIT;
