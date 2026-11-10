# connect/snapshot-restart-mid

Task restart and then a worker kill in the middle of an incremental snapshot (1024-row chunks, 60000 rows): the snapshot resumes from the chunk key in the offsets and every row arrives.
