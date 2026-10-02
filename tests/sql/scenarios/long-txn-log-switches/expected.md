The long transaction (session 1) starts in one archived log and commits in the fourth. OLR must
stitch its redo together across the sequences. The short transaction in session 2 commits in
the second log and must be output first, as its own transaction. Expected long transaction:
5 updates (ids 1..5), 500 inserts (1001..1500), 100 deletes (1001..1100, the rows it inserted
itself), 1 update (id 6). The deleted rows exist in the before-image only inside the
transaction, the net effect after commit is 400 new rows.
