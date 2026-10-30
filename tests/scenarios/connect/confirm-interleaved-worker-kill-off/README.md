Variant of `connect/confirm-interleaved-worker-kill` with `openlogreplicator.confirm.completed.transactions=false` (schema `OLRT_CF1O`): the task confirms and continues at the start of its offset SCN, so OLR must log `client requested scn: ..., idx: 0` and no CONTINUE with a higher index. Delivery, diff and replay must pass as with the option on.

T2 (session 2) begins and writes a row, T1 (session 1) begins later and commits first. Once T1 is on the
topic and confirmed (heartbeat at its commit, offset flush), 50 commits on a table outside the filter give OLR
checkpoints past T1. The Connect worker is then SIGKILLed, and T2 commits after the task resumed. The task
continues (CONTINUE) from the position confirmed for T1; T2 must arrive complete, T1 not again, and OLR must
not log WARN 60039 (a CONTINUE before the confirmed position).
