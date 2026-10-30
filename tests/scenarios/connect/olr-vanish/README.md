OLR replaced while the client's connection goes silent, as in dev on 2026-10-29: an Argo sync replaced the OLR pod,
no FIN or RST reached Debezium, its socket stayed ESTABLISHED with no timer, the task stayed RUNNING and no offset
moved until the task was restarted by hand. In docker an OLR restart closes the connection properly, so
`olr-restart` never showed this.

The `olr = "vanish"` step drops the inbound packets of the established connection in the Connect container's
network namespace (iptables, `docker/netadmin`), then restarts OLR (SIGINT, writer checkpoint deleted). Probes and
CONFIRMs leave but nothing comes back, the same as a DNAT to a deleted pod; new connections are not affected.

Expected with TCP keepalive on the OLR connection (idle 60 s, interval 10 s, count 6): the read fails with an
IOException about 120 s after the last traffic, the task restarts by itself (retriable) and rows 11 and 12 arrive.
Without keepalive the task stays RUNNING and rows 11 and 12 never arrive (known issue on versions without it).
