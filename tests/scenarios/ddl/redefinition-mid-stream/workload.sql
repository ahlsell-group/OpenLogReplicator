UPDATE olrt_ddlr1.t SET qty = 1.5, txt = 'before redef' WHERE id = 1;
COMMIT;
-- a job of small committed transactions on T that runs while START_REDEF_TABLE copies (MARKS shows the overlap)
BEGIN
  DBMS_SCHEDULER.CREATE_JOB(job_name => 'OLRT_DDLR1.DML_JOB', job_type => 'PLSQL_BLOCK', enabled => TRUE, auto_drop => TRUE,
    job_action => q'[BEGIN
      FOR i IN 1 .. 400 LOOP
        UPDATE olrt_ddlr1.t SET qty = qty + 1000, txt = 'job ' || i WHERE id = 1000 + i;
        INSERT INTO olrt_ddlr1.t VALUES (300000 + i, i, 'job insert ' || i);
        IF MOD(i, 10) = 0 THEN DELETE FROM olrt_ddlr1.t WHERE id = 2000 + i; END IF;
        INSERT INTO olrt_ddlr1.marks (what, id) VALUES ('job', i);
        COMMIT;
        DBMS_SESSION.SLEEP(0.01);
      END LOOP;
    END;]');
END;
/
-- wait until the job has committed its first transaction
DECLARE n NUMBER; BEGIN
  FOR k IN 1 .. 600 LOOP SELECT COUNT(*) INTO n FROM olrt_ddlr1.marks WHERE what = 'job'; EXIT WHEN n > 0; DBMS_SESSION.SLEEP(0.05); END LOOP;
END;
/
INSERT INTO olrt_ddlr1.marks (what) VALUES ('start_redef begin');
COMMIT;
BEGIN DBMS_REDEFINITION.START_REDEF_TABLE('OLRT_DDLR1', 'T', 'T_INT', NULL, DBMS_REDEFINITION.CONS_USE_PK); END;
/
INSERT INTO olrt_ddlr1.marks (what) VALUES ('start_redef end');
COMMIT;
-- DML between START and COPY_TABLE_DEPENDENTS / SYNC
UPDATE olrt_ddlr1.t SET qty = -2, txt = 'after start' WHERE id = 2;
INSERT INTO olrt_ddlr1.t VALUES (400001, 1, 'after start');
DELETE FROM olrt_ddlr1.t WHERE id = 3;
COMMIT;
DECLARE n PLS_INTEGER; BEGIN
  DBMS_REDEFINITION.COPY_TABLE_DEPENDENTS('OLRT_DDLR1', 'T', 'T_INT', DBMS_REDEFINITION.CONS_ORIG_PARAMS, TRUE, TRUE, TRUE, FALSE, n);
  IF n != 0 THEN RAISE_APPLICATION_ERROR(-20001, 'COPY_TABLE_DEPENDENTS errors: ' || n); END IF;
END;
/
-- the job must be done before SYNC, so FINISH does not wait on it
DECLARE n NUMBER; BEGIN
  FOR k IN 1 .. 1200 LOOP SELECT COUNT(*) INTO n FROM dba_scheduler_running_jobs WHERE owner = 'OLRT_DDLR1'; EXIT WHEN n = 0; DBMS_SESSION.SLEEP(0.1); END LOOP;
  IF n != 0 THEN RAISE_APPLICATION_ERROR(-20002, 'DML_JOB still running'); END IF;
END;
/
BEGIN DBMS_REDEFINITION.SYNC_INTERIM_TABLE('OLRT_DDLR1', 'T', 'T_INT'); END;
/
-- DML between SYNC and FINISH (committed: FINISH waits for open transactions)
UPDATE olrt_ddlr1.t SET qty = -4, txt = 'after sync' WHERE id = 4;
UPDATE olrt_ddlr1.t SET txt = 'after sync' WHERE id = 400001;
INSERT INTO olrt_ddlr1.t VALUES (400002, 2, 'after sync');
DELETE FROM olrt_ddlr1.t WHERE id = 5;
COMMIT;
BEGIN DBMS_REDEFINITION.FINISH_REDEF_TABLE('OLRT_DDLR1', 'T', 'T_INT'); END;
/
INSERT INTO olrt_ddlr1.marks (what) VALUES ('finish_redef end');
COMMIT;
-- after the swap T is the former interim object: UPDATE/DELETE of rows from before, during and after the redefinition
UPDATE olrt_ddlr1.t SET qty = 7.125, txt = 'after finish' WHERE id IN (1, 6, 1001, 300001, 400001, 400002);
DELETE FROM olrt_ddlr1.t WHERE id IN (7, 1002, 300002);
INSERT INTO olrt_ddlr1.t VALUES (400003, 3, 'after finish');
COMMIT;
DROP TABLE olrt_ddlr1.t_int PURGE;
UPDATE olrt_ddlr1.t SET txt = 'after drop' WHERE id IN (8, 400003);
DELETE FROM olrt_ddlr1.t WHERE id = 9;
COMMIT;
