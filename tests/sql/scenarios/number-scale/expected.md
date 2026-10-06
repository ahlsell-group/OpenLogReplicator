Values must arrive as the exact decimal Oracle stores, with no float rounding and no digits
beyond what the column holds. Zero in a `NUMBER(15,3)` column is `0` (or `0.000`; the replay
check compares the numeric value, Oracle stores both identically).

- id 1: inserted as all zeros, then `qty` becomes 0.001 and `anynum` 1/3 (0.333... with 40 digits)
- id 2: negatives and the smallest 38-digit integer; later `qty2` NULL and `anynum` 2/3
- id 3: largest `NUMBER(15,3)`, a 39-digit `NUMBER`, 38-digit integer, `FLOAT` 1e125; deleted at the end
- id 4: 1e-20 in a `NUMBER` column (the extreme 1e-130 is in `number-extreme-exponent`)
- id 5: all NULL, then `qty` 0.5 and `qty2` -0.000 (stored as 0)
- ids 100, 101: update to and from zero
