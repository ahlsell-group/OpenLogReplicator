Oracle NUMBER spans 1e-130 to 9.99e125. Expected: the exact value in the create and update
events, not 0.

Oracle stores positive values from 1e-130 up to (not including) 1e-128 with the exponent byte
0x80 followed by mantissa bytes; a lone 0x80 byte is zero. OLR 2.0.0 took every value starting
with 0x80 for zero, so 1e-130, 1.5e-129 and 9.99e-129 were written as `0` and the replayed row
differed from the database. Negative values (exponent byte 0x7F) and 1e-128 were right.
Fixed in the fork by "Decode NUMBER values below 1e-128 instead of writing 0"; OLR 2.0.0 still
fails this scenario.
