UPDATE olrt_cr2.t SET qty = 1.5, txt = 'before redef' WHERE id = 1;
COMMIT;
-- redefinition 1 (storage only), with a job of small transactions on T during the copy
BEGIN
  DBMS_SCHEDULER.CREATE_JOB(job_name => 'OLRT_CR2.DML_JOB', job_type => 'PLSQL_BLOCK', enabled => TRUE, auto_drop => TRUE,
    job_action => q'[BEGIN
      FOR i IN 1 .. 200 LOOP
        UPDATE olrt_cr2.t SET qty = qty + 1000, txt = 'job ' || i WHERE id = 1000 + i;
        INSERT INTO olrt_cr2.t VALUES (30000 + i, i, 'job insert ' || i, DATE '2026-06-01');
        IF MOD(i, 10) = 0 THEN DELETE FROM olrt_cr2.t WHERE id = 2000 + i; END IF;
        INSERT INTO olrt_cr2.marks (what, id) VALUES ('job', i);
        COMMIT;
        DBMS_SESSION.SLEEP(0.005);
      END LOOP;
    END;]');
END;
/
DECLARE n NUMBER; BEGIN
  FOR k IN 1 .. 600 LOOP SELECT COUNT(*) INTO n FROM olrt_cr2.marks WHERE what = 'job'; EXIT WHEN n > 0; DBMS_SESSION.SLEEP(0.05); END LOOP;
END;
/
INSERT INTO olrt_cr2.marks (what) VALUES ('start_redef begin');
COMMIT;
BEGIN DBMS_REDEFINITION.START_REDEF_TABLE('OLRT_CR2', 'T', 'T_INT1', NULL, DBMS_REDEFINITION.CONS_USE_PK); END;
/
INSERT INTO olrt_cr2.marks (what) VALUES ('start_redef end');
COMMIT;
UPDATE olrt_cr2.t SET qty = -2, txt = 'after start 1' WHERE id = 2;
INSERT INTO olrt_cr2.t VALUES (40001, 1, 'after start 1', DATE '2026-07-01');
DELETE FROM olrt_cr2.t WHERE id = 3;
COMMIT;
DECLARE n PLS_INTEGER; BEGIN
  DBMS_REDEFINITION.COPY_TABLE_DEPENDENTS('OLRT_CR2', 'T', 'T_INT1', DBMS_REDEFINITION.CONS_ORIG_PARAMS, TRUE, TRUE, TRUE, FALSE, n);
  IF n != 0 THEN RAISE_APPLICATION_ERROR(-20001, 'COPY_TABLE_DEPENDENTS errors: ' || n); END IF;
END;
/
DECLARE n NUMBER; BEGIN
  FOR k IN 1 .. 1200 LOOP SELECT COUNT(*) INTO n FROM dba_scheduler_running_jobs WHERE owner = 'OLRT_CR2'; EXIT WHEN n = 0; DBMS_SESSION.SLEEP(0.1); END LOOP;
  IF n != 0 THEN RAISE_APPLICATION_ERROR(-20002, 'DML_JOB still running'); END IF;
END;
/
BEGIN DBMS_REDEFINITION.SYNC_INTERIM_TABLE('OLRT_CR2', 'T', 'T_INT1'); END;
/
UPDATE olrt_cr2.t SET qty = -4, txt = 'after sync 1' WHERE id IN (4, 40001);
DELETE FROM olrt_cr2.t WHERE id = 5;
COMMIT;
BEGIN DBMS_REDEFINITION.FINISH_REDEF_TABLE('OLRT_CR2', 'T', 'T_INT1'); END;
/
UPDATE olrt_cr2.t SET qty = 6.5, txt = 'after finish 1' WHERE id IN (6, 1001, 30001, 40001);
DELETE FROM olrt_cr2.t WHERE id IN (7, 1002, 30002);
INSERT INTO olrt_cr2.t VALUES (40002, 2, 'after finish 1', DATE '2026-08-01');
COMMIT;
DROP TABLE olrt_cr2.t_int1 PURGE;
-- redefinition 2 (layout change)
BEGIN DBMS_REDEFINITION.START_REDEF_TABLE('OLRT_CR2', 'T', 'T_INT2', 'id id, txt txt, d d, qty qty', DBMS_REDEFINITION.CONS_USE_PK); END;
/
UPDATE olrt_cr2.t SET qty = -8, txt = 'after start 2' WHERE id = 8;
INSERT INTO olrt_cr2.t VALUES (40003, 3, 'after start 2', DATE '2026-09-01');
COMMIT;
DECLARE n PLS_INTEGER; BEGIN
  DBMS_REDEFINITION.COPY_TABLE_DEPENDENTS('OLRT_CR2', 'T', 'T_INT2', DBMS_REDEFINITION.CONS_ORIG_PARAMS, TRUE, TRUE, TRUE, FALSE, n);
  IF n != 0 THEN RAISE_APPLICATION_ERROR(-20001, 'COPY_TABLE_DEPENDENTS errors: ' || n); END IF;
END;
/
BEGIN DBMS_REDEFINITION.SYNC_INTERIM_TABLE('OLRT_CR2', 'T', 'T_INT2'); END;
/
UPDATE olrt_cr2.t SET qty = -9, txt = 'after sync 2' WHERE id = 9;
COMMIT;
BEGIN DBMS_REDEFINITION.FINISH_REDEF_TABLE('OLRT_CR2', 'T', 'T_INT2'); END;
/
-- new layout (id, txt, d, qty, extra)
UPDATE olrt_cr2.t SET qty = 7.1255, txt = RPAD('after finish 2', 80, 'y'), extra = 'set' WHERE id IN (1, 10, 1003, 30003, 40001, 40003);
DELETE FROM olrt_cr2.t WHERE id IN (11, 1004, 40002);
INSERT INTO olrt_cr2.t VALUES (40004, 'after finish 2', DATE '2027-01-01', 4.0001, 'new');
COMMIT;
DROP TABLE olrt_cr2.t_int2 PURGE;
UPDATE olrt_cr2.t SET txt = 'after drop 2' WHERE id IN (12, 40004);
COMMIT;
