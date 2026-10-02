-- One DELETE on WIDE_B (row ending at C63, C64/C65 NULL), then UPDATEs of the last column of WIDE_A,
-- in one transaction.
DELETE FROM rt_wide.wide_b WHERE id = 7;
UPDATE rt_wide.wide_a SET c64 = 7.5 WHERE id = 3;
UPDATE rt_wide.wide_a SET c64 = 8.25 WHERE id = 4;
COMMIT;
