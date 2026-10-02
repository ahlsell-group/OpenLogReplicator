Both tables are in the OLR filter. Expected output, one transaction:

- `d` on `RT_WIDE.WIDE_B`: before-image of id 7 with C2..C225 set (C226..C313 are NULL or absent)
- `u` on `RT_WIDE.WIDE_A` id 3: C2 `XX`, C3 2026-10-02

Regression: OLR 2.0.0 stops while processing the UPDATE with
`ERROR 50073 runtime error ... table: RT_WIDE.WIDE_A: missmatch in column details: 278 < 278`,
so the transaction is never committed in the output. The failure needs both tables in the
filter, a DELETE of a row on the >256-column table whose trailing columns are NULL, and a
following change to the other >256-column table.
