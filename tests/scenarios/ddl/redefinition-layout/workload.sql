UPDATE olrt_ddlr3.t SET qty = 1.5, txt = 'before redef', code = 1 WHERE id = 1;
COMMIT;
-- a job of small committed transactions on T that runs while START_REDEF_TABLE copies (MARKS shows the overlap)
BEGIN
  DBMS_SCHEDULER.CREATE_JOB(job_name => 'OLRT_DDLR3.DML_JOB', job_type => 'PLSQL_BLOCK', enabled => TRUE, auto_drop => TRUE,
    job_action => q'[BEGIN
      FOR i IN 1 .. 400 LOOP
        UPDATE olrt_ddlr3.t SET qty = qty + 1000, txt = 'job ' || i, code = i WHERE id = 1000 + i;
        INSERT INTO olrt_ddlr3.t VALUES (300000 + i, i, 'job insert ' || i, i, DATE '2026-06-01');
        IF MOD(i, 10) = 0 THEN DELETE FROM olrt_ddlr3.t WHERE id = 2000 + i; END IF;
        INSERT INTO olrt_ddlr3.marks (what, id) VALUES ('job', i);
        COMMIT;
        DBMS_SESSION.SLEEP(0.01);
      END LOOP;
    END;]');
END;
/
DECLARE n NUMBER; BEGIN
  FOR k IN 1 .. 600 LOOP SELECT COUNT(*) INTO n FROM olrt_ddlr3.marks WHERE what = 'job'; EXIT WHEN n > 0; DBMS_SESSION.SLEEP(0.05); END LOOP;
END;
/
INSERT INTO olrt_ddlr3.marks (what) VALUES ('start_redef begin');
COMMIT;
BEGIN DBMS_REDEFINITION.START_REDEF_TABLE('OLRT_DDLR3', 'T', 'T_INT', 'id id, txt txt, qty qty, d d, code code', DBMS_REDEFINITION.CONS_USE_PK); END;
/
INSERT INTO olrt_ddlr3.marks (what) VALUES ('start_redef end');
COMMIT;
UPDATE olrt_ddlr3.t SET qty = -2, txt = 'after start', code = 22 WHERE id = 2;
INSERT INTO olrt_ddlr3.t VALUES (400001, 1, 'after start', 41, DATE '2026-07-01');
DELETE FROM olrt_ddlr3.t WHERE id = 3;
COMMIT;
DECLARE n PLS_INTEGER; BEGIN
  DBMS_REDEFINITION.COPY_TABLE_DEPENDENTS('OLRT_DDLR3', 'T', 'T_INT', DBMS_REDEFINITION.CONS_ORIG_PARAMS, TRUE, TRUE, TRUE, FALSE, n);
  IF n != 0 THEN RAISE_APPLICATION_ERROR(-20001, 'COPY_TABLE_DEPENDENTS errors: ' || n); END IF;
END;
/
DECLARE n NUMBER; BEGIN
  FOR k IN 1 .. 1200 LOOP SELECT COUNT(*) INTO n FROM dba_scheduler_running_jobs WHERE owner = 'OLRT_DDLR3'; EXIT WHEN n = 0; DBMS_SESSION.SLEEP(0.1); END LOOP;
  IF n != 0 THEN RAISE_APPLICATION_ERROR(-20002, 'DML_JOB still running'); END IF;
END;
/
BEGIN DBMS_REDEFINITION.SYNC_INTERIM_TABLE('OLRT_DDLR3', 'T', 'T_INT'); END;
/
UPDATE olrt_ddlr3.t SET qty = -4, txt = 'after sync', code = 44 WHERE id = 4;
INSERT INTO olrt_ddlr3.t VALUES (400002, 2, 'after sync', 42, DATE '2026-07-02');
DELETE FROM olrt_ddlr3.t WHERE id = 5;
COMMIT;
BEGIN DBMS_REDEFINITION.FINISH_REDEF_TABLE('OLRT_DDLR3', 'T', 'T_INT'); END;
/
INSERT INTO olrt_ddlr3.marks (what) VALUES ('finish_redef end');
COMMIT;
-- new layout: (id, txt, qty, d, code, extra)
UPDATE olrt_ddlr3.t SET qty = 7.1255, txt = RPAD('after finish', 80, 'y'), code = 99999999999, d = DATE '2027-03-04', extra = 'set' WHERE id IN (1, 6, 1001, 300001, 400001, 400002);
UPDATE olrt_ddlr3.t SET qty = qty + 1 WHERE id = 10;
DELETE FROM olrt_ddlr3.t WHERE id IN (7, 1002, 300002);
INSERT INTO olrt_ddlr3.t (id, txt, qty, code) VALUES (400003, 'after finish', 3.0001, 123);
INSERT INTO olrt_ddlr3.t VALUES (400004, 'after finish', 4, DATE '2027-01-01', NULL, 'x');
COMMIT;
DROP TABLE olrt_ddlr3.t_int PURGE;
UPDATE olrt_ddlr3.t SET txt = 'after drop' WHERE id IN (8, 400003);
DELETE FROM olrt_ddlr3.t WHERE id = 9;
COMMIT;
