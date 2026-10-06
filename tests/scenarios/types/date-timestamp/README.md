DATE and TIMESTAMP values must arrive as the stored wall time (no zone shift: upstream #321
reports DB timezone shifts). Covers dates before 1970 (negative epoch), 0001-01-01 and
9999-12-31, the 02:30 that does not exist in Berlin on 2026-03-29 (stored as plain wall
time) and every fraction precision.

Row 5 is a leap day; row 100 exists before the start SCN and is updated to a DST wall time.
