Smallest shape of the phantom transaction on Debezium's LogMiner adapter: a storage-only redefinition of a
10-row table with one UPDATE (id 1, QTY 1 -> -1) between START and FINISH. Besides the real UPDATE the topic
gets a second transaction: DELETE id 1 (before QTY = 1, stale) and INSERT id 1 (QTY = -1), which is FINISH
refreshing the interim table from the materialized view log. The end state is right; the extra events are not.
