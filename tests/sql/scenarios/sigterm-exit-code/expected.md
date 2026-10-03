# sigterm-exit-code

OLR runs during the workload (no `stop-log-switches`). Once its output has not grown for 5 s the runner stops
the container with `docker stop`, which sends SIGTERM and SIGKILL 30 s later.

Expected: OLR handles SIGTERM like SIGINT (`caught signal: 15`), stops cleanly and exits with 0. The output
replays as usual.

What it guards: OpenLogReplicator 2.0.0 handles SIGINT only. As PID 1 in a container it ignores SIGTERM, so
`docker stop` (and Kubernetes) waits for the timeout and kills it: exit code 137, no final checkpoint.
