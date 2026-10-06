Both tables are in the OLR filter. Expected output, one transaction:

- `d` on `RT_WIDE.WIDE_B`: before-image of id 7 with C2..C63 set (C64 and C65 are NULL or absent)
- `u` on `RT_WIDE.WIDE_A` id 3: C64 7.5
- `u` on `RT_WIDE.WIDE_A` id 4: C64 8.25

Regression: OLR 2.0.0 stops while processing the first UPDATE with
`ERROR 50073 runtime error ... table: RT_WIDE.WIDE_A: missmatch in column details: 64 < 64`,
so the transaction is never committed in the output. The trigger is a DELETE of a row whose last
non-NULL column (63) is just below a 64-column boundary, followed in the same transaction by DML on
another table that reaches past it (column 64). The width does not have to exceed 255 columns; this is
the smallest shape found (65/64 columns, row ending at 63).
