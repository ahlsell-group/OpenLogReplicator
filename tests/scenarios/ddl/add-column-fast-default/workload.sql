ALTER TABLE olrt_ddl9.t ADD (flag VARCHAR2(1) DEFAULT 'N' NOT NULL, qty NUMBER(15,3) DEFAULT 0.000 NOT NULL);
UPDATE olrt_ddl9.t SET a = 'upd' WHERE id <= 5;
UPDATE olrt_ddl9.t SET flag = 'Y' WHERE id BETWEEN 6 AND 10;
DELETE FROM olrt_ddl9.t WHERE id BETWEEN 11 AND 15;
INSERT INTO olrt_ddl9.t (id, a) VALUES (100, 'new');
UPDATE olrt_ddl9.t SET a = 'all' WHERE id BETWEEN 16 AND 30;
COMMIT;
-- 313th column on a 312-column table: crosses the 255 and the row-piece boundary
ALTER TABLE olrt_ddl9.w ADD (c313 NUMBER(15,3) DEFAULT 7.5 NOT NULL);
UPDATE olrt_ddl9.w SET c3 = 'upd' WHERE id <= 10;
UPDATE olrt_ddl9.w SET c313 = 1 WHERE id BETWEEN 11 AND 15;
UPDATE olrt_ddl9.w SET c312 = 'high' WHERE id BETWEEN 16 AND 20;
DELETE FROM olrt_ddl9.w WHERE id BETWEEN 21 AND 25;
INSERT INTO olrt_ddl9.w (id, c3) VALUES (100, 'new');
UPDATE olrt_ddl9.w SET c3 = 'all' WHERE id BETWEEN 26 AND 30;
COMMIT;
