Dev 2026-10-06 (E9 in the cutover runbook): OLR had been scaled to 0 for three hours while the connector ran LogMiner,
so its newest checkpoint was three hours old. On the switch back it started from that checkpoint, logged the right first
data SCN, and still re-sent three whole transactions committed well before it. This scenario builds the same shape: OLR
writes a checkpoint, is stopped, the connector swaps to LogMiner, several short transactions commit and one stays open,
then a fresh OLR (writer checkpoint deleted) gets START at LogMiner's offset. The delivery check counts every row of a
transaction committed before the START SCN as a duplicate.
