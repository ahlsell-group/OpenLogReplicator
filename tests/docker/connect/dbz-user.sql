-- Debezium's JDBC user (schema reads, snapshots). Common user, run in CDB$ROOT by
-- olrt.connect.ensure_dbz_user when C##DBZUSER does not exist yet. Grants as in Debezium's
-- Oracle docs, minus the LogMiner-only ones (the OLR adapter never mines).
CREATE USER c##dbzuser IDENTIFIED BY dbz DEFAULT TABLESPACE users QUOTA UNLIMITED ON users CONTAINER=ALL;
GRANT CREATE SESSION TO c##dbzuser CONTAINER=ALL;
GRANT SET CONTAINER TO c##dbzuser CONTAINER=ALL;
GRANT SELECT ON V_$DATABASE TO c##dbzuser CONTAINER=ALL;
GRANT SELECT ANY TABLE TO c##dbzuser CONTAINER=ALL;
GRANT FLASHBACK ANY TABLE TO c##dbzuser CONTAINER=ALL;
GRANT SELECT ANY DICTIONARY TO c##dbzuser CONTAINER=ALL;
GRANT SELECT_CATALOG_ROLE TO c##dbzuser CONTAINER=ALL;
GRANT LOCK ANY TABLE TO c##dbzuser CONTAINER=ALL;
GRANT SELECT ANY TRANSACTION TO c##dbzuser CONTAINER=ALL;
GRANT SELECT ON V_$TRANSACTION TO c##dbzuser CONTAINER=ALL;
