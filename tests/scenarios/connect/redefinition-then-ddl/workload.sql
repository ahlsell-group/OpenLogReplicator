UPDATE olrt_cr6.t SET qty = 2.125, txt = 'old layout' WHERE id = 2;
COMMIT;
BEGIN DBMS_REDEFINITION.START_REDEF_TABLE('OLRT_CR6', 'T', 'T_INT', 'id id, txt txt, qty qty', DBMS_REDEFINITION.CONS_USE_PK); END;
/
UPDATE olrt_cr6.t SET qty = -3, txt = 'after start' WHERE id = 3;
COMMIT;
DECLARE n PLS_INTEGER; BEGIN
  DBMS_REDEFINITION.COPY_TABLE_DEPENDENTS('OLRT_CR6', 'T', 'T_INT', DBMS_REDEFINITION.CONS_ORIG_PARAMS, TRUE, TRUE, TRUE, FALSE, n);
  IF n != 0 THEN RAISE_APPLICATION_ERROR(-20001, 'COPY_TABLE_DEPENDENTS errors: ' || n); END IF;
END;
/
BEGIN DBMS_REDEFINITION.FINISH_REDEF_TABLE('OLRT_CR6', 'T', 'T_INT'); END;
/
-- layout after the swap: (id, txt, qty NUMBER(18,4), extra)
UPDATE olrt_cr6.t SET qty = 7.1255, txt = RPAD('after finish', 80, 'y'), extra = 'set' WHERE id IN (1, 10);
INSERT INTO olrt_cr6.t VALUES (3001, 'after finish', 4.0001, 'new');
DELETE FROM olrt_cr6.t WHERE id = 11;
COMMIT;
DROP TABLE olrt_cr6.t_int PURGE;
-- changed again before the connector reaches the rows above
ALTER TABLE olrt_cr6.t MODIFY (qty NUMBER(20,6));
UPDATE olrt_cr6.t SET qty = 1.123456, extra = 'modified' WHERE id IN (12, 3001);
INSERT INTO olrt_cr6.t VALUES (3002, 'after modify', 2.654321, 'm');
COMMIT;
ALTER TABLE olrt_cr6.t DROP COLUMN extra;
UPDATE olrt_cr6.t SET qty = 3.5, txt = 'after drop column' WHERE id IN (13, 3002);
INSERT INTO olrt_cr6.t VALUES (3003, 'after drop column', 9.000001);
COMMIT;
