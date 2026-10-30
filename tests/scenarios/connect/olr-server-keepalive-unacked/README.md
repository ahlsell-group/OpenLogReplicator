`olr-server-keepalive` with one more step: after the task's connection is silenced and the task restarted, a DDL on a
table that is not captured. With SHOW_DDL (flags 32) OLR sends every DDL in the database as a message, so it writes
this one to the dead connection, where it stays unacknowledged (Send-Q 506, retransmission timer instead of the
keepalive timer in `ss -tnoi`). Linux sends no keepalive probes while data is unacknowledged: the connection fails
only when the retransmissions give up (`tcp_retries2` = 15, about 15 minutes). Until then OLR does not answer the
restarted task's INFO, and rows 11 and 12 do not arrive.

Oracle's own DDL does the same at random: the AWR purge alters the sequence `SYS.WRM$_DEEP_PURGE_INTERVAL`, and when
that lands in the quiet period it turned `olr-server-keepalive` red about 1 run in 3 with the `debezium-ddl` profile.

The expectation is the keepalive one (WARN 10061 "Connection timed out" within the wait). The known issue stays until
OLR bounds unacknowledged data too, e.g. `TCP_USER_TIMEOUT` = idle + interval x count on the accepted socket in
`StreamNetwork`, which fails the connection with the same ETIMEDOUT.
