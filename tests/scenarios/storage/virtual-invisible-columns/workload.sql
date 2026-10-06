INSERT INTO olrt_vc.t (id, a, b, tag, txt) VALUES (100, 1, 2, 'new-tag', 'new');
INSERT INTO olrt_vc.t (id, a, b, txt) VALUES (101, 3, 4, 'x');
UPDATE olrt_vc.t SET a = a * 10 WHERE id <= 5;
UPDATE olrt_vc.t SET tag = 'upd-tag' WHERE id BETWEEN 6 AND 10;
UPDATE olrt_vc.t SET txt = NULL WHERE id BETWEEN 11 AND 12;
DELETE FROM olrt_vc.t WHERE id BETWEEN 13 AND 15;
COMMIT;
