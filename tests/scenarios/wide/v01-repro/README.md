repro order, date C3, noop low, delete B. The shape that triggers ERROR 50073: DELETE of a WIDE_313 row whose columns from segment 257 on are NULL, then an UPDATE on WIDE_278 touching a column above 256. Must FAIL on 2.0.0 and PASS on v2.0.0-ahlsell.1.

Wide-table variant (filter on WIDE_278 and WIDE_313 together; the bug needs both tables in the filter). Guards ERROR 50073 after DELETE on a >256-column table (fixed in v2.0.0-ahlsell.1, branch fix/50073-delete-valuesmax).
