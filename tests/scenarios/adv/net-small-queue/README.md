The queue-size deadlock (upstream bersler/OpenLogReplicator#24, F2 in docs/findings-adversarial.md) in a cheap form: queue-size 200 instead of 65 536, a 1 000-row transaction followed by a short
one. Debezium 3.6.1 confirms only when the SCN grows, and every message of one transaction carries the
same c_scn, so a transaction longer than queue-size fills the queue with nothing the client will ever
confirm. Shows whether the fork's commit-SCN stamping (upstream #330) changes anything (begin-SCN
stamping looks like a possible root cause).

With fork fix/queue-full-warning the deadlock stays (it is the queue bound plus Debezium 3.6.1's
confirm rule), but once the queue has been full for 10 s with messages of the scn about to be sent, OLR logs
`WARN 60040 output queue full for 10s (queue-size: 200) with messages of one transaction, scn: ...` and
repeats it every minute, so the stall shows in the log.
