INSERT INTO olrt_aut.t VALUES (1, 'outer-1');
BEGIN olrt_aut.autolog(100, 'auto-1'); END;
/
INSERT INTO olrt_aut.t VALUES (2, 'outer-2');
BEGIN olrt_aut.autolog(101, 'auto-2'); olrt_aut.autolog(102, 'auto-3'); END;
/
UPDATE olrt_aut.t SET txt = 'outer-1b' WHERE id = 1;
COMMIT;
-- outer rolls back, autonomous work survives
INSERT INTO olrt_aut.t VALUES (3, 'outer-3');
BEGIN olrt_aut.autolog(103, 'auto-4'); END;
/
ROLLBACK;
