The connector starts on the OLR adapter. Session 2 opens a transaction and writes two rows, another transaction
commits (OLR delivers it, the offset scn becomes its commit SCN), then the connector is PUT with
`database.connection.adapter=logminer` (same name, offsets kept) and OLR is stopped. Session 2 writes one more row
and commits. LogMiner mines from the offset scn, so the two rows written before the swap are missing from the
delivered transaction: a silent partial transaction, documented as a known issue. The rewind variant fixes it.
