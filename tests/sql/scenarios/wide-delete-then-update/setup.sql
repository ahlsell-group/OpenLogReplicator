BEGIN EXECUTE IMMEDIATE 'DROP USER rt_wide CASCADE';
EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1918 THEN RAISE; END IF; END;
/
CREATE USER rt_wide IDENTIFIED BY test QUOTA UNLIMITED ON users DEFAULT TABLESPACE users;
-- WIDE_A has 278 columns, WIDE_B 313: both need more than 256 column slots, so their rows are
-- stored in several row pieces. Column type cycles by position: C2 VARCHAR2, C3 DATE, C4 NUMBER, ...
-- WIDE_B rows have values up to C225 only; C226..C313 are trailing NULLs.
DECLARE
    PROCEDURE make(p_table VARCHAR2, p_cols PLS_INTEGER, p_filled PLS_INTEGER, p_first PLS_INTEGER, p_rows PLS_INTEGER) IS
        ddl  VARCHAR2(30000) := 'ID NUMBER(10) NOT NULL';
        cols VARCHAR2(30000) := 'ID';
        vals VARCHAR2(30000) := 'l';
    BEGIN
        FOR i IN 2 .. p_cols LOOP
            ddl  := ddl  || ', C' || i || CASE MOD(i, 3) WHEN 0 THEN ' DATE' WHEN 1 THEN ' NUMBER(15,3)' ELSE ' VARCHAR2(40)' END;
        END LOOP;
        FOR i IN 2 .. p_filled LOOP
            cols := cols || ', C' || i;
            vals := vals || CASE MOD(i, 3)
                WHEN 0 THEN ', DATE ''2026-01-01'' + MOD(l * ' || i || ', 300)'
                WHEN 1 THEN ', l * 1000 + ' || i || ' + 0.125'
                ELSE ', ''R'' || l || ''_C' || i || ''''
            END;
        END LOOP;
        EXECUTE IMMEDIATE 'CREATE TABLE rt_wide.' || p_table || ' (' || ddl || ', CONSTRAINT ' || p_table || '_pk PRIMARY KEY (id))';
        EXECUTE IMMEDIATE 'INSERT INTO rt_wide.' || p_table || ' (' || cols || ') SELECT ' || vals
            || ' FROM (SELECT ' || p_first || ' + LEVEL - 1 l FROM dual CONNECT BY LEVEL <= ' || p_rows || ')';
        EXECUTE IMMEDIATE 'ALTER TABLE rt_wide.' || p_table || ' ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS';
        COMMIT;
    END;
BEGIN
    make('WIDE_A', 278, 278, 1, 50);
    make('WIDE_B', 313, 225, 1, 50);
END;
/
