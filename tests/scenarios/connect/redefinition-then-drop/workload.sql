UPDATE olrt_cr7.t SET qty = 2.125, txt = 'old layout' WHERE id = 2;
COMMIT;
BEGIN DBMS_REDEFINITION.START_REDEF_TABLE('OLRT_CR7', 'T', 'T_INT', 'id id, txt txt, qty qty', DBMS_REDEFINITION.CONS_USE_PK); END;
/
DECLARE n PLS_INTEGER; BEGIN
  DBMS_REDEFINITION.COPY_TABLE_DEPENDENTS('OLRT_CR7', 'T', 'T_INT', DBMS_REDEFINITION.CONS_ORIG_PARAMS, TRUE, TRUE, TRUE, FALSE, n);
  IF n != 0 THEN RAISE_APPLICATION_ERROR(-20001, 'COPY_TABLE_DEPENDENTS errors: ' || n); END IF;
END;
/
BEGIN DBMS_REDEFINITION.FINISH_REDEF_TABLE('OLRT_CR7', 'T', 'T_INT'); END;
/
-- layout after the swap: (id, txt, qty NUMBER(18,4), extra)
UPDATE olrt_cr7.t SET qty = 7.1255, txt = RPAD('after finish', 80, 'y'), extra = 'set' WHERE id IN (1, 10);
INSERT INTO olrt_cr7.t VALUES (3001, 'after finish', 4.0001, 'new');
DELETE FROM olrt_cr7.t WHERE id = 11;
COMMIT;
DROP TABLE olrt_cr7.t_int PURGE;
-- gone before the connector reaches the rows above
DROP TABLE olrt_cr7.t PURGE;
