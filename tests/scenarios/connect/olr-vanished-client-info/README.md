A client that vanished without a FIN or RST reaching OpenLogReplicator, while OLR keeps running. OLR's network
writer serves one client (`StreamNetwork::isConnected`, `listen(serverFD, 1)`, no keepalive on the accepted socket)
and does not accept another while it holds one. The kernel still completes the TCP handshake of a new connection
from the backlog, so the client's INFO is sent but never read.

The `olr = "silence"` step drops the packets of the established connection in both directions in the Connect
container (iptables, `docker/netadmin`), so neither the client's FIN on close nor anything else reaches OLR. The
task is then restarted: the old client closes (FIN dropped), the new one connects and sends INFO.

Expected on 3.7.0.Final-ahlsell.8 and later: with `openlogreplicator.info.timeout.ms` 15 s the task logs "did not
answer the INFO request within 15000 ms", fails retriably and connects again, so the wait is visible. Row 11 is
committed while OLR still sends to the old socket. After `unsilence` the old connection fails on OLR's side (the
client's FIN or a RST for OLR's retransmission), OLR accepts the next connection and rows 11 and 12 arrive.

Earlier versions block in the INFO read without a timeout and log nothing; they recover at the same point when the
connection waiting in the backlog is the one OLR serves, so only the `expect_log` step fails there. In production
without `unsilence` OLR notices the dead socket only by its retransmission timeout while it has data to send
(about 15 minutes on Linux), and never while the database is quiet; the timeout makes that wait visible, it does
not shorten it.
