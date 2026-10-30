Counterpart to `olr-vanish`: with TCP keepalive on the OLR connection, a quiet period (OLR sends nothing, no
SHOW_CHECKPOINT) longer than the keepalive idle time must not fail the connection. A live OLR's kernel answers the
probes. The `netns` steps record the connection (`ss -tno`: same local port, `timer:(keepalive,...)`) before,
during and after 330 s of silence; the Connect log must have no producer failure or retry.
