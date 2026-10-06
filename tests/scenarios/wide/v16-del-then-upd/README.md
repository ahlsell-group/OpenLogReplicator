minimal: delete B, update A (date+status). Minimal 50073 trigger: one DELETE on WIDE_313 (trailing NULLs), one UPDATE on WIDE_278.

Wide-table variant (filter on WIDE_278 and WIDE_313 together; the bug needs both tables in the filter). Guards ERROR 50073 after DELETE on a >256-column table (fixed in v2.0.0-ahlsell.1, branch fix/50073-delete-valuesmax).
