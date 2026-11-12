A new snapshot while OLR keeps running: the connector's offsets are deleted (stop, DELETE /offsets, resume), it
snapshots again with snapshot.mode=no_data and, since OLR answers INFO with REPLICATE, continues with CONTINUE(new
snapshot SCN, c_idx 0). A transaction updated a row before the new snapshot SCN and commits after it. Expected:
all its rows arrive. Debezium sets skipToStartScn whenever the offset has no index, so the row below the new
snapshot SCN is expected to be dropped (known issue, Debezium side).
