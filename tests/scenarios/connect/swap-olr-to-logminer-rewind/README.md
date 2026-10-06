As swap-olr-to-logminer, but the swap step first reads the oldest open transaction's start SCN from
v$transaction, stops the connector, patches its offset (PATCH /offsets) to scn = that SCN, snapshot_scn = the same
and commit_scn = "<old offset scn>:1:", then PUTs the LogMiner config and resumes. LogMiner re-mines from before the
open transaction, skips the transactions it already has (commit SCN at or below the old offset) and delivers the open
transaction in full: no loss, no duplicates. Reading v$transaction before the stop keeps it race-free: a transaction
starting later is covered by the mining start, one committing before the stop is skipped via commit_scn.
