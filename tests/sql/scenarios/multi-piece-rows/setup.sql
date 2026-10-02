BEGIN EXECUTE IMMEDIATE 'DROP USER rt_piece CASCADE';
EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1918 THEN RAISE; END IF; END;
/
CREATE USER rt_piece IDENTIFIED BY test QUOTA UNLIMITED ON users DEFAULT TABLESPACE users;
-- 300 columns: every row is stored in two row pieces. PCTFREE 0 and short strings pack the
-- blocks full, so growing a row later forces migration to another block.
-- Column type cycles by position: C2 VARCHAR2, C3 DATE, C4 NUMBER(15,3), C5 VARCHAR2, ...
DECLARE
    ddl  VARCHAR2(30000) := 'ID NUMBER(10) NOT NULL';
    cols VARCHAR2(30000) := 'ID';
    vals VARCHAR2(30000) := 'l';
BEGIN
    FOR i IN 2 .. 300 LOOP
        ddl  := ddl  || ', C' || i || CASE MOD(i, 3) WHEN 0 THEN ' DATE' WHEN 1 THEN ' NUMBER(15,3)' ELSE ' VARCHAR2(60)' END;
        cols := cols || ', C' || i;
        vals := vals || CASE MOD(i, 3)
            WHEN 0 THEN ', DATE ''2026-01-01'' + MOD(l * ' || i || ', 300)'
            WHEN 1 THEN ', l * 1000 + ' || i || ' + 0.125'
            ELSE ', ''s' || i || ''''
        END;
    END LOOP;
    EXECUTE IMMEDIATE 'CREATE TABLE rt_piece.wide_c (' || ddl || ', CONSTRAINT wide_c_pk PRIMARY KEY (id)) PCTFREE 0';
    EXECUTE IMMEDIATE 'INSERT INTO rt_piece.wide_c (' || cols || ') SELECT ' || vals
        || ' FROM (SELECT LEVEL l FROM dual CONNECT BY LEVEL <= 100)';
    EXECUTE IMMEDIATE 'ALTER TABLE rt_piece.wide_c ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS';
    COMMIT;
END;
/
