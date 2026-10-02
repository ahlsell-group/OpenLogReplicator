-- One DELETE on WIDE_B (row with trailing NULLs), then an UPDATE on WIDE_A, in one transaction.
DELETE FROM rt_wide.wide_b WHERE id = 7;
UPDATE rt_wide.wide_a SET c2 = 'XX', c3 = DATE '2026-10-02' WHERE id = 3;
COMMIT;
