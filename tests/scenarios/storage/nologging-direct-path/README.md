Documents why FORCE LOGGING matters: an APPEND insert into a
NOLOGGING table writes no row redo, so neither OLR nor LogMiner can see it. Replay fails by
design; the scenario is marked as a known issue so a change in behaviour shows as XPASS.
