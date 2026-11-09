Review probe (not generated). Run 1: session 2 inserts into T and stays open over a checkpoint; OLR is
killed and min-tran removed, so run 2 starts after the begin of session 2 (unknown transaction, as after
a cold start whose low watermark does not cover it). Run 2: session 3 updates U (outside the filter) and
stays open over a checkpoint, which therefore carries dormant-tran. Kill, run 3 restarts past the dormant
transaction and recreates session 3. Session 2 then inserts into T and commits: its rows are lost either
way; the question is which warning names it. The dormant state was right (session 3 is recreated and
delivers), so the expected warning is 60011, not 60061 "dormant state was wrong".
