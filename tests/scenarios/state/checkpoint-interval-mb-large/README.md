Control for `state/checkpoint-every-loop`: identical workload, restart and assertions, but
`state.interval-mb: 1048576`, so only `interval-s` (1800) and log switches trigger a metadata checkpoint.
The checkpoint written at the log switch before the client's offset carries the open transaction as
`min-tran`, so the coverage assertion must pass. If this passes and checkpoint-every-loop fails, the
difference is `interval-mb: 0`; a very large value is the way to get the "time trigger only" behaviour
in 2.0.0.
