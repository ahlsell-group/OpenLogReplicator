The connector starts on the LogMiner adapter with OLR not running. Session 2 opens a transaction and writes two rows,
another transaction commits, then a fresh OLR is started (READY, no checkpoint) and the connector is PUT with the OLR
adapter. LogMiner's offset scn is the start of the oldest open transaction, so OLR gets START there and sends every
transaction committed at or after it in full: the open transaction arrives whole and the already delivered one again
(whole-transaction duplicates, reported by the delivery check, no loss).
