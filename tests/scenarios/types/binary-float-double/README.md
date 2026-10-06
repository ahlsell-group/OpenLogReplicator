Debezium issues 1679/1681 (whole numbers in BINARY_FLOAT/DOUBLE) and upstream PR 318
(subnormals). Rarely used types; low priority, kept because it is cheap.

Rows 4 and 5 use NUMBER literals, so `4.9e-324` and `-0.0` arrive as 0. Rows 6-9 use typed
literals (`f`/`d`): the double subnormal, the extremes and values that need 9/17 significant
digits. `-0.0f` reads back as `0.0` through the driver, so -0 is covered by the fork's unit test
(BinaryFloatDecodeTest), not here.

Root cause (fixed in fork branch fix/binary-float-double): BuilderJson wrote the values with
std::ostream's default 6 significant digits (16777216 -> 1.67772e+07), and decodeFloat/decodeDouble
used exponent -127/-1023 for subnormals, which halved them (1e-40 -> 5e-41).

Rows 10-12 repeat the precision limits and subnormals with typed literals and add a typed zero.
