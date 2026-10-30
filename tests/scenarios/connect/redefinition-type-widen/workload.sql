UPDATE olrt_cr9.t SET ts = DATE '2026-02-01', code = 'a1', vc = 'äöåäöåäöåä', nv = N'äöåäöåäöåä' WHERE id = 1;
INSERT INTO olrt_cr9.t VALUES (3, DATE '2026-01-03', 'c', 3.75, 3, 3.33, 1.5, 'c', N'c', N'z', TIMESTAMP '2026-01-03 10:00:00.001');
COMMIT;
BEGIN DBMS_REDEFINITION.START_REDEF_TABLE('OLRT_CR9', 'T', 'T_INT',
  'id id, CAST(ts AS TIMESTAMP(6)) ts, code code, n n, ni ni, ns ns, f f, vc vc, nv nv, nc nc, t3 t3', DBMS_REDEFINITION.CONS_USE_PK); END;
/
UPDATE olrt_cr9.t SET code = 'b1' WHERE id = 2;
COMMIT;
DECLARE n PLS_INTEGER; BEGIN
  DBMS_REDEFINITION.COPY_TABLE_DEPENDENTS('OLRT_CR9', 'T', 'T_INT', DBMS_REDEFINITION.CONS_ORIG_PARAMS, TRUE, TRUE, TRUE, FALSE, n);
END;
/
BEGIN DBMS_REDEFINITION.FINISH_REDEF_TABLE('OLRT_CR9', 'T', 'T_INT'); END;
/
UPDATE olrt_cr9.t SET ts = TIMESTAMP '2026-03-01 12:34:56.123456', code = 'longer than ten' WHERE id = 1;
INSERT INTO olrt_cr9.t VALUES (4, TIMESTAMP '2026-03-02 01:02:03.000004', 'forty-character code value, ok', 4, 4, 4.44, 4.5, 'd', N'd', N'w',
                               TIMESTAMP '2026-03-02 10:00:00.999');
COMMIT;
UPDATE olrt_cr9.t SET n = 5 WHERE id = 3;
DELETE FROM olrt_cr9.t WHERE id = 2;
COMMIT;
