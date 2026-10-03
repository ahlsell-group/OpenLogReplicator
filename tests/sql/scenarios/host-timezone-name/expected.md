# host-timezone-name

Redo log timestamps are the wall-clock time of the database host, without a zone. The reader option
`host-timezone` tells OLR that zone; here it is set to the zone name the database container runs with
(`OLRSQL_ORACLE_TZ`, default `Europe/Berlin`). Three transactions store `SYS_EXTRACT_UTC(SYSTIMESTAMP)` in the
row they change.

Expected: OLR starts, and the `tm` of each transaction's begin message (UTC, nanoseconds) is within 5 s of the
UTC time stored in its row (`commit_time_column`). Values replay as usual.

What it guards: a fixed offset such as `+01:00` is right for only part of the year in a zone with daylight
saving time, so `tm` was off by an hour for the rest of it. With a zone name the offset follows the date of
each timestamp. OpenLogReplicator 2.0.0 accepts only `+HH:MM` and stops with `ERROR 30001 ... invalid
"host-timezone" value`.

This test runs on one date, so it does not cross a daylight saving change; the conversion around the
transitions is covered by the unit test `TimeZoneTest`.
