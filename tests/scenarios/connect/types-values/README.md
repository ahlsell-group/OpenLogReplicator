The value side of the Connect mode: NUMBER(15,3) with 0.000 and 12.500 (Debezium must restore the
scale from the schema, OLR sends 0 and 12.5), NUMBER values below 1e-128 (exponent byte 0x80) and
at 9.99e125, and BINARY_FLOAT/DOUBLE values that need 9/17 significant digits or are subnormal.
kafka.jsonl shows the decoded Avro values with their scale. Upstream 2.0.0 writes the values below
1e-128 as 0 and BINARY_FLOAT/DOUBLE with 6 significant digits; fork branches fix/number-tiny and
fix/binary-float-double fix both.
