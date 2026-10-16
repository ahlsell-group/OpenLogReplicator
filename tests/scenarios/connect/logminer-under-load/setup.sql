BEGIN EXECUTE IMMEDIATE 'DROP USER olrt_rl CASCADE';
EXCEPTION WHEN OTHERS THEN IF SQLCODE != -1918 THEN RAISE; END IF; END;
/
CREATE USER olrt_rl IDENTIFIED BY olrt QUOTA UNLIMITED ON users DEFAULT TABLESPACE users;
CREATE TABLE olrt_rl.localpart (
    partno       VARCHAR2(20) NOT NULL,
    compno       VARCHAR2(3)  NOT NULL,
    qtyavailable NUMBER(15,3),
    qtyreserved  NUMBER(15,3),
    unitprice    NUMBER(15,3),
    purchstop    VARCHAR2(1),
    killdate     DATE,
    changed      DATE,
    descr        VARCHAR2(40),
    stockno      NUMBER(5),
    CONSTRAINT localpart_pk PRIMARY KEY (partno, compno)
);
ALTER TABLE olrt_rl.localpart ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
INSERT INTO olrt_rl.localpart
SELECT TO_CHAR(LEVEL), '1', MOD(LEVEL, 7) * 1.5, 0, LEVEL / 8, 'N', NULL, SYSDATE, 'part ' || LEVEL, MOD(LEVEL, 40)
  FROM dual CONNECT BY LEVEL <= 3000;
COMMIT;
