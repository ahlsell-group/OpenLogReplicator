As net-kill-mid-big-txn, but the writer checkpoint is deleted before the restart (as an init
container might do), so Debezium sends START with the commit SCN of the partly delivered transaction as offset.
Whether OLR resends that transaction (it began before the START SCN) decides between duplicates and loss.
