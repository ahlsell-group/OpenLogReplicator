Counterpart to `adv/net-client-vanish-quiet`: with server-side TCP keepalive on OLR's client connection, a quiet
period (nothing sent, no SHOW_CHECKPOINT) longer than the keepalive time (60 s + 6 x 10 s) must not drop a live client.
The client's kernel answers the probes. After 150 s of silence row 11 must arrive on the same connection, and OLR's log
must show no network error or disconnect before the end.
