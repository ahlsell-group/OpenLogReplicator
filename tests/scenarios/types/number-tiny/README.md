Pinpoints where OLR stops decoding small NUMBER values: types/number-scale showed 1e-130
arriving as 0. Values step through the exponent range, positive and negative; rows 103-105 add
the top of the range (9.99e125, 1e125) and the bottom pair (9.99e-129).

Root cause (fixed in fork branch fix/number-tiny): Oracle stores positive values below 1e-128 with
exponent byte 0x80 plus mantissa bytes, and Builder::parseNumber took every value starting with
0x80 for zero. Negative values (exponent byte 0x7F) were always right.

Row 1000 is updated from 1 to 1e-130 and 1e-128, so the smallest value also appears in an update's
after-image.
