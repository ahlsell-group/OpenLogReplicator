As confirm-interleaved-worker-kill, but OLR is restarted (SIGINT) with its writer checkpoint deleted, as
the dev deployment does, so the restarted task sends START with the offset SCN (T1's commit SCN). T2 began
before that SCN; OLR reads it again from its own checkpoint's min-tran and must send it complete.
