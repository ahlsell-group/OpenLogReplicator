UPDATE olrt_pk.t SET id = 1001 WHERE id = 1;
UPDATE olrt_pk.t SET id = id + 5000, qty = 0 WHERE id BETWEEN 2 AND 4;
COMMIT;
