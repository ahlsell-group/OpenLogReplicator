BEGIN EXECUTE IMMEDIATE 'DROP USER olrt_k6 CASCADE';
EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1918 THEN RAISE; END IF; END;
/
CREATE USER olrt_k6 IDENTIFIED BY olrt QUOTA UNLIMITED ON users DEFAULT TABLESPACE users;
GRANT EXECUTE ON DBMS_SESSION TO olrt_k6;
CREATE TABLE olrt_k6.dbz_signal (id VARCHAR2(42) PRIMARY KEY, type VARCHAR2(32) NOT NULL, data VARCHAR2(2048));
ALTER TABLE olrt_k6.dbz_signal ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
GRANT SELECT, INSERT, DELETE ON olrt_k6.dbz_signal TO c##dbzuser;
DECLARE
  ddl VARCHAR2(8000) := 'CREATE TABLE olrt_k6.t (compno NUMBER(5) NOT NULL, compid VARCHAR2(20) NOT NULL';
  cols VARCHAR2(8000) := 'compno, compid';
  vals VARCHAR2(8000) := 'MOD(LEVEL, 7) + 1, ''ID'' || LPAD(LEVEL, 10, ''0'')';
BEGIN
  FOR i IN 1..10 LOOP ddl := ddl || ', n' || i || ' NUMBER(15,3)'; END LOOP;
  FOR i IN 1..10 LOOP ddl := ddl || ', v' || i || ' VARCHAR2(30)'; END LOOP;
  EXECUTE IMMEDIATE ddl || ', CONSTRAINT t_pk PRIMARY KEY (compno, compid))';
  EXECUTE IMMEDIATE 'ALTER TABLE olrt_k6.t ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS';
  FOR i IN 1..10 LOOP cols := cols || ', n' || i; vals := vals || ', LEVEL * ' || i || ' / 1000'; END LOOP;
  FOR i IN 1..10 LOOP cols := cols || ', v' || i; vals := vals || ', ''v' || i || '-'' || LEVEL'; END LOOP;
  EXECUTE IMMEDIATE 'INSERT /*+ APPEND */ INTO olrt_k6.t (' || cols || ') SELECT ' || vals || ' FROM dual CONNECT BY LEVEL <= 60000';
  COMMIT;
END;
/
CREATE TABLE olrt_k6.stream (id NUMBER(10) PRIMARY KEY, txt VARCHAR2(40));
ALTER TABLE olrt_k6.stream ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
