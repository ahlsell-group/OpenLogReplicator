The version rollback after a failed prd hop: the connector runs on Debezium 3.7.0 with LogMiner (offsets with
window_advance_enabled, a 3.7.0 schema history), then the worker is replaced by 3.2.2 on the same group and storage
topics. 3.2.2 must load the offset (unknown keys ignored) and the schema history, skip the snapshot and resume from
LogMiner's scn: the open transaction arrives whole, the last delivered transaction at most once more. OLR is not
involved; the scenario runs without an OLR container. Needs an Oracle whose banner 3.2.2 accepts (23ai).
