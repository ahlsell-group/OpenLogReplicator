INSERT INTO rt_term.t SELECT LEVEL, 'row ' || LEVEL FROM dual CONNECT BY LEVEL <= 10;
COMMIT;
UPDATE rt_term.t SET v = 'updated' WHERE id <= 3;
COMMIT;
