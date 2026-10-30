T2 (session 2) writes a row and stays open; T1 commits; 1 500 single-LWN commits outside the filter
(queue-size 200); OLR is restarted with its writer checkpoint deleted (START at the offset SCN, T1's commit,
after T2 began), then the worker is SIGKILLed; then T2 writes another row and commits, and T3 follows. T2 must
arrive with both rows: the offset SCN must not have moved past T2's begin in a way OLR cannot recover.
