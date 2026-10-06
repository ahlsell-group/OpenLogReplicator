A without MOVE (dataobj=obj). WIDE_278_NM was never MOVEd, so dataobj = obj (the other variants have dataobj != obj).

Wide-table variant (filter on WIDE_278_NM and WIDE_313 together; the bug needs both tables in the filter). Guards ERROR 50073 after DELETE on a >256-column table (fixed in v2.0.0-ahlsell.1, branch fix/50073-delete-valuesmax).
