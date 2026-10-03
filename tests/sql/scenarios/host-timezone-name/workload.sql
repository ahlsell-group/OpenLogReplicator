-- Each change stores the UTC time it was made at; the runner compares it with the transaction's tm.
INSERT INTO rt_tz.t VALUES (1, SYS_EXTRACT_UTC(SYSTIMESTAMP));
COMMIT;
-- @sleep 2
INSERT INTO rt_tz.t VALUES (2, SYS_EXTRACT_UTC(SYSTIMESTAMP));
COMMIT;
-- @sleep 2
UPDATE rt_tz.t SET utc_time = SYS_EXTRACT_UTC(SYSTIMESTAMP) WHERE id = 1;
COMMIT;
