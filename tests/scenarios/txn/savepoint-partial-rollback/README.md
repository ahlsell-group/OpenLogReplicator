Changes after the savepoint are rolled back, the rest commits. OLR must drop exactly the
rolled-back changes (a partial rollback it cannot pair logs WARN 70003
and leaves the change in the output as a phantom).

After ROLLBACK TO SAVEPOINT b the same row is changed again and kept, a third savepoint undoes an insert and an update, and a second transaction is
rolled back as a whole. Expected: one transaction with exactly three updates (id 1, id 4, id 5 'kept').
