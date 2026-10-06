Upstream #323: multi-row insert on a compressed table. Direct-path (APPEND) into a BASIC
compressed table writes compressed blocks; later updates decompress rows (row migration).
Runs with FORCE LOGGING so the direct-path load is in redo.
