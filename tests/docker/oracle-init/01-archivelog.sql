-- ARCHIVELOG + FRA + minimal supplemental logging. Runs once as SYS in CDB$ROOT.
-- Database-level logging is the minimum a CDC source needs: supplemental_log_data_min=YES,
-- no PK logging, no FORCE LOGGING. Tables get ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS
-- in their own setup SQL; FORCE LOGGING can be switched on per scenario.
WHENEVER SQLERROR EXIT FAILURE
ALTER SYSTEM SET db_recovery_file_dest_size = 20G SCOPE=BOTH;
ALTER SYSTEM SET db_recovery_file_dest = '/opt/oracle/oradata/fra' SCOPE=BOTH;

SHUTDOWN IMMEDIATE;
STARTUP MOUNT;
ALTER DATABASE ARCHIVELOG;
ALTER DATABASE OPEN;

ALTER DATABASE ADD SUPPLEMENTAL LOG DATA;
-- FREEPDB1 is already open here (gvenzl opens it); keep it open across restarts.
ALTER PLUGGABLE DATABASE FREEPDB1 SAVE STATE;

ALTER SYSTEM ARCHIVE LOG CURRENT;
SELECT log_mode, force_logging, supplemental_log_data_min, supplemental_log_data_pk FROM v$database;
