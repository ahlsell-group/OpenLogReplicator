DATE and TIMESTAMP values far from the epoch through real Debezium: DATE 0001-01-01 (OLR sends
-62135596800000000000 ns, beyond int64, which Debezium reads as a BigInteger), a three-digit
year, the Julian/Gregorian switch (1582-10-04/15) and 9999-12-31 for TIMESTAMP(0/3/6).
kafka.jsonl holds the epoch values Debezium writes (io.debezium.time.Timestamp/MicroTimestamp/
NanoTimestamp), compared with LogMiner and the database as proleptic Gregorian wall time.

TIMESTAMP(7-9) becomes io.debezium.time.NanoTimestamp (int64 ns, 1677-09-21 00:12:43.145224192 to
2262-04-11 23:47:16.854775807). Debezium 3.7.0 writes a value it finds outside that range as NULL
with a WARN "Failed to convert value"; its range check rejects 1677-09-21 00:12:44 as well, so T9
holds 1677-09-22 and the exact upper end, which it accepts.
