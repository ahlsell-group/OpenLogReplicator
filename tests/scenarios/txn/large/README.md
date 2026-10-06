Volume inside a single transaction: memory/swap paths and output ordering. Batch jobs can
write hundreds of thousands of rows in one transaction; the network-writer queue-size deadlock
(adv/net-small-queue) needs the network mode.
