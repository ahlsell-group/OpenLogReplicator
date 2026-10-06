control: as v01 but deleted B row fully populated. Control: the deleted WIDE_313 row is fully populated (no trailing NULLs).

Wide-table variant (filter on WIDE_278 and WIDE_313 together; the bug needs both tables in the filter). Guards ERROR 50073 after DELETE on a >256-column table (fixed in v2.0.0-ahlsell.1, branch fix/50073-delete-valuesmax).
