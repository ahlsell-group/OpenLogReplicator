Interleaved sessions (copy of txn/interleaved) with an exact XID check: with the debezium profile
(xid format 3) every transaction's "xid" must be one of LogMiner's RAWTOHEX(XID) values, without the
byte-order fallback the other checks use. Guards fork fix/xid-logminer-byte-order on little-endian
hosts; the big-endian case needs a big-endian (POWER/SPARC) database and is covered by the fork's unit tests.
