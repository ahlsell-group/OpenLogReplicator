Session 2 inserts into T and stays open over a checkpoint. OLR is killed and `drop_min_tran` removes
min-tran from every checkpoint, so the warm start begins at the checkpoint and never reads the begin of
the open transaction. After the start the transaction inserts into T again and commits. Without flag
1024 OLR keeps nothing of a transaction whose begin it did not read, so its rows are lost by
construction (check run only); the point is that the loss is loud: WARN 60011 must name the xid at the
commit (fix/loud-unknown-transaction). Images without the fix drop it silently (known issue).
