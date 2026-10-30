A Debezium task whose connection vanished without a FIN or RST reaching OpenLogReplicator, while no captured table
changes. OLR's network writer serves one client and accepts the next only after it has dropped the current one; the
kernel completes the handshake of the restarted task from the listen backlog, so the task's INFO is sent but not read.
With hidden checkpoints OLR sends nothing while the database is quiet, so without TCP keepalive nothing ever fails on
the dead connection.

The `olr = "silence"` step drops the packets of the task's established connection in both directions in the Connect
container (iptables, `docker/netadmin`), then the task is restarted: the old client's FIN is dropped, the new one
connects and sends INFO. Nothing is committed for 150 s. With server-side keepalive (writer `tcp-keepalive-idle-s` 60,
`-interval-s` 10, `-count` 6, the defaults) OLR's probes go unanswered, the read fails with ETIMEDOUT about 120 s after
the last traffic (WARN 10061 "Connection timed out"), OLR closes the connection and answers the waiting task's INFO.
Row 11, committed after that, must arrive within 120 s; row 12 after `unsilence` too.

OLR runs without SHOW_DDL (`flags = 0`) here. With it, OLR sends every DDL in the database, also Oracle's own:
the AWR purge alters the sequence `SYS.WRM$_DEEP_PURGE_INTERVAL` now and then. A DDL message written to the dead
connection stays unacknowledged, Linux sends no keepalive probes while data is unacknowledged, and the connection
fails only by the retransmission timeout (about 15 minutes); that is how this scenario failed about 1 run in 3 with
the `debezium-ddl` profile. `olr-server-keepalive-unacked` forces that case. The `silence` step also waits until OLR's
Send-Q on the connection is 0 before it drops the packets.

Without keepalive (OLR before .11) the dead connection is held; row 11 is then sent to it and the connection fails
only by the retransmission timeout (about 15 minutes), so row 11 does not arrive in time. Compare
`olr-vanished-client-info` (a row sent while OLR holds the dead connection: the retransmission timeout, not
keepalive, frees it) and `adv/net-client-vanish-quiet` (the same case with the emulated client, runs in CI).
