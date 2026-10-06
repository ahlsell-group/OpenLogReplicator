Production's path: the connector runs on Debezium 3.2.2 with LogMiner, then the worker is replaced by 3.7.0 (same
group and storage topics, offsets and config kept) and the connector is PUT with the OLR adapter against a fresh OLR.
3.7.0's OLR offset loader reads only scn and scn_idx from the 3.2.2 offset, so this behaves as swap-logminer-to-olr:
the open transaction arrives whole, the last LogMiner-delivered transaction may come again. Run with `--dbz 3.2.2.Final`;
the upgrade step inside the scenario brings in 3.7.0.
