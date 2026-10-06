Session 2 opens a transaction and writes a row; session 1 commits something in between each of session 2's statements
so that the rows of the open transaction land at distinct SCNs; session 2 commits. The `scn` check compares, per
transaction, the multiset of `source.scn` on the topics with LogMiner's per-row SCNs. Stock Debezium 3.6.1/3.7.0
gives every row the transaction's c_scn (known issue); the Ahlsell connector build carries the row's own SCN.
