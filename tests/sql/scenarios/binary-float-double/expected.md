BINARY_FLOAT and BINARY_DOUBLE are IEEE 754 values. Expected: every value reads back as the same
float (single precision) or double.

Observed with OLR 2.0.0: the JSON text has 6 significant digits, so 16777216 is written as
`1.67772e+07` and 9007199254740992 as `9.0072e+15`, and subnormals are halved (1e-40 becomes
`5e-41`, 4.9e-324 becomes `0`). Fixed in the fork by "Write BINARY_FLOAT/BINARY_DOUBLE exactly in
JSON"; OLR 2.0.0 still fails this scenario.
