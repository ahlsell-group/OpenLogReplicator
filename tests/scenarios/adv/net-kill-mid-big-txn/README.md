OLR crashes (SIGKILL, like an OOM kill) while it is streaming a 20 500-row transaction over a log switch;
the client has stored a position 700 messages into it and reconnects. OLR restarts from its own
checkpoint with the writer checkpoint kept, so Debezium takes the CONTINUE path with (c_scn, c_idx)
inside the transaction. The rest of the transaction must arrive once.
