-- OLR user in the PDB. Grants as in OLR's installation docs (installation.adoc) plus the
-- undocumented XDB objects. OLR never reads row data over SQL, only the dictionary.
WHENEVER SQLERROR EXIT FAILURE
ALTER SESSION SET CONTAINER=FREEPDB1;

CREATE USER olr IDENTIFIED BY "olr" DEFAULT TABLESPACE users;
GRANT CREATE SESSION TO olr;

BEGIN
    FOR t IN (SELECT column_value AS n FROM TABLE(sys.odcivarchar2list(
                'CCOL$', 'CDEF$', 'COL$', 'DEFERRED_STG$', 'ECOL$', 'LOB$', 'LOBCOMPPART$',
                'LOBFRAG$', 'OBJ$', 'TAB$', 'TABCOMPART$', 'TABPART$', 'TABSUBPART$', 'TS$', 'USER$'))) LOOP
        EXECUTE IMMEDIATE 'GRANT SELECT, FLASHBACK ON SYS."' || t.n || '" TO olr';
    END LOOP;
    FOR t IN (SELECT column_value AS n FROM TABLE(sys.odcivarchar2list(
                'V_$ARCHIVED_LOG', 'V_$DATABASE', 'V_$DATABASE_INCARNATION', 'V_$LOG', 'V_$LOGFILE',
                'V_$PDBS', 'V_$PARAMETER', 'V_$STANDBY_LOG', 'V_$TRANSPORTABLE_PLATFORM'))) LOOP
        EXECUTE IMMEDIATE 'GRANT SELECT ON SYS."' || t.n || '" TO olr';
    END LOOP;
    EXECUTE IMMEDIATE 'GRANT SELECT, FLASHBACK ON XDB.XDB$TTSET TO olr';
    FOR t IN (SELECT table_name FROM dba_tables
               WHERE owner = 'XDB'
                 AND (table_name LIKE 'X$NM%' OR table_name LIKE 'X$PT%' OR table_name LIKE 'X$QN%')) LOOP
        EXECUTE IMMEDIATE 'GRANT SELECT, FLASHBACK ON XDB."' || t.table_name || '" TO olr';
    END LOOP;
END;
/

-- Needed by the fork's cold-start fix (fix/cold-start-low-watermark) to find the oldest open
-- transaction; upstream 2.0.0 does not read it.
GRANT SELECT ON SYS.V_$TRANSACTION TO olr;
