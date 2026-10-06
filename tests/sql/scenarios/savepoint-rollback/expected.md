Changes undone by `ROLLBACK TO SAVEPOINT` must not appear in the output, and the part of the
transaction before and after the savepoints must. Expected: one transaction with exactly three
updates: id 1 `qty` 10, id 4 `qty` 11, id 5 `txt` 'kept'. No event for ids 2, 3, 6, 7, 200,
201, 202. The fully rolled-back transaction produces no output at all.
