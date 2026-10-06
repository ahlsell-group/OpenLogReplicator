A migrated/chained rows, PCTFREE 0. WIDE_278_CH has PCTFREE 0 and grown rows, so updates hit migrated/chained rows.

Wide-table variant (filter on WIDE_278_CH and WIDE_313 together; the bug needs both tables in the filter). Guards ERROR 50073 after DELETE on a >256-column table (fixed in v2.0.0-ahlsell.1, branch fix/50073-delete-valuesmax).
