UPDATE olrt_ddl13.t SET a = 'before' WHERE id = 1;
COMMIT;
ALTER TABLE olrt_ddl13.t RENAME TO t_new;
UPDATE olrt_ddl13.t_new SET qty = qty * 2 WHERE id <= 3;
INSERT INTO olrt_ddl13.t_new VALUES (21, 'n21', 21, DATE '2027-01-02');
DELETE FROM olrt_ddl13.t_new WHERE id = 5;
COMMIT;
