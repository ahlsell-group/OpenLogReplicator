-- Runs as SYS in CDB$ROOT (container = "root"): a common user for OLR with the grants of
-- docker/init/02-olr-user.sql, and a common user owning the table.
BEGIN EXECUTE IMMEDIATE 'DROP USER c##rt_olr CASCADE';
EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1918 THEN RAISE; END IF; END;
/
BEGIN EXECUTE IMMEDIATE 'DROP USER c##rt_root CASCADE';
EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1918 THEN RAISE; END IF; END;
/
CREATE USER c##rt_olr IDENTIFIED BY "olr" DEFAULT TABLESPACE users CONTAINER = ALL;
GRANT CREATE SESSION TO c##rt_olr;
BEGIN
    FOR t IN (SELECT column_value AS n FROM TABLE(sys.odcivarchar2list(
                'CCOL$', 'CDEF$', 'COL$', 'DEFERRED_STG$', 'ECOL$', 'LOB$', 'LOBCOMPPART$',
                'LOBFRAG$', 'OBJ$', 'TAB$', 'TABCOMPART$', 'TABPART$', 'TABSUBPART$', 'TS$', 'USER$'))) LOOP
        EXECUTE IMMEDIATE 'GRANT SELECT, FLASHBACK ON SYS."' || t.n || '" TO c##rt_olr';
    END LOOP;
    FOR t IN (SELECT column_value AS n FROM TABLE(sys.odcivarchar2list(
                'V_$ARCHIVED_LOG', 'V_$DATABASE', 'V_$DATABASE_INCARNATION', 'V_$LOG', 'V_$LOGFILE',
                'V_$PDBS', 'V_$PARAMETER', 'V_$STANDBY_LOG', 'V_$TRANSACTION', 'V_$TRANSPORTABLE_PLATFORM'))) LOOP
        EXECUTE IMMEDIATE 'GRANT SELECT ON SYS."' || t.n || '" TO c##rt_olr';
    END LOOP;
    EXECUTE IMMEDIATE 'GRANT SELECT, FLASHBACK ON XDB.XDB$TTSET TO c##rt_olr';
    FOR t IN (SELECT table_name FROM dba_tables
               WHERE owner = 'XDB'
                 AND (table_name LIKE 'X$NM%' OR table_name LIKE 'X$PT%' OR table_name LIKE 'X$QN%')) LOOP
        EXECUTE IMMEDIATE 'GRANT SELECT, FLASHBACK ON XDB."' || t.table_name || '" TO c##rt_olr';
    END LOOP;
END;
/
CREATE USER c##rt_root IDENTIFIED BY test QUOTA UNLIMITED ON users DEFAULT TABLESPACE users;
CREATE TABLE c##rt_root.t (id NUMBER(10) PRIMARY KEY, v VARCHAR2(40));
ALTER TABLE c##rt_root.t ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
INSERT INTO c##rt_root.t VALUES (1, 'existing');
COMMIT;
