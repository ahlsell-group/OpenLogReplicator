Guards the CONTINUE gap check against positions only hidden checkpoints reached (fork
`fix/coalesce-hidden-checkpoints`).

Without SHOW_CHECKPOINT OLR confirms its checkpoints itself, so its confirmed position passes the client's offset during
an idle period. A client that reconnects from that offset (Debezium after a task restart) is before the confirmed
position, and the gap check (WARN 60039, `continue-gap: 1` refuses) asks whether messages after the client's position
were released. Checkpoints are no longer written to the builder buffers, so with no captured change since OLR started
the buffers hold no message at all; judged against the hidden checkpoints' position that looked like "everything up to
the confirmed position was released". The check now uses the newest confirmed message from the buffers: none here,
so no gap. Runs with `continue-gap: 1`, so a wrong answer fails the reconnect and the row never arrives.
