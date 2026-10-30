As confirm-interleaved-olr-restart, but the writer checkpoint is kept: OLR resumes from the position it
had confirmed (T1's commit or a later checkpoint) and the restarted task continues (CONTINUE) from the
completed position in its offset. T2 began before both and must arrive complete.
