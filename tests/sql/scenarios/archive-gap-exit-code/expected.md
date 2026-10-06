# archive-gap-exit-code

Three transactions in three archived logs. Before OLR runs, the second archived log is renamed in the
database container (and put back afterwards), so the reader cannot open it.

Expected: OLR logs an error about the file (`ERROR 1000x`) and ends with a non-zero exit code. Only the run is
checked; the output is incomplete by design.

What it guards: an error in the reader or replicator thread stopped the process, but `main()` returned 0. A
supervisor (systemd, Kubernetes) took that for a normal end and neither restarted nor alerted. OpenLogReplicator
2.0.0 ends this run with exit code 0. The unit test `ExitCodeTest` covers the same without a database.
