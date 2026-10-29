A client that vanished without a FIN or RST reaching OpenLogReplicator, during a quiet period. OLR's network writer
serves one client (`StreamNetwork::isConnected` accepts only while it holds none); the kernel completes the handshake
of the next client from the listen backlog, so that client's INFO is sent but not read until OLR drops the old
connection. Since hidden checkpoints are not sent, OLR sends nothing while no captured table changes, so without TCP
keepalive no send or read ever fails on the dead connection.

The `olr = "silence"` step drops the packets of OLR's established client connection in both directions in OLR's own
network namespace (iptables, `docker/netadmin`); the client then closes (its FIN is dropped) and a new client connects.
With server-side keepalive (writer `tcp-keepalive-idle-s` 60, `-interval-s` 10, `-count` 6, the defaults) the read on
the dead connection fails with ETIMEDOUT about 120 s after the last traffic: OLR logs WARN 10061 "Connection timed
out", closes it, accepts the waiting client and answers its INFO; the CONTINUE resumes after row 10 and row 11 arrives.
Without keepalive the new client gets no answer within 240 s ("run" fails) and row 11 is missing.

The Debezium counterpart is `connect/olr-server-keepalive`.
