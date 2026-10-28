Guards builder memory while a client keeps a transaction unconfirmed (fork `fix/coalesce-hidden-checkpoints`).

`adv/net-quiet-checkpoints` shows that the self-confirmed checkpoints must not fill the writer queue. Their messages
still sat in the builder buffers, which are released in order: behind the held transaction every checkpoint of the
idle period stayed in memory (about 230 B per LWN; 6 GB on dev is 2-3 quiet days). Here the builder gets 4 MB
(`write-buffer-max-mb`), the client confirms like Debezium 3.7.0, and 30 000 single-LWN commits on a table outside
the filter follow. With checkpoint messages in the buffers the parser waits for memory after about 18 000 LWNs and the
next captured row does not arrive within 60 s (a read timeout fails "run"). Without SHOW_CHECKPOINT the builder now
keeps only the checkpoint's position, so memory stays flat and the row arrives.
