Changes after the savepoint are rolled back, the rest commits. OLR must drop exactly the
rolled-back changes (a partial rollback it cannot pair logs WARN 70003
and leaves the change in the output as a phantom).
