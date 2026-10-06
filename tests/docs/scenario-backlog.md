# Scenario backlog

Backlog of regression scenarios for OpenLogReplicator (OLR) as used with the Debezium 3.6.1 OLR adapter against Oracle 19c. Merged from five research notes: an Oracle redo edge-case catalog, mined Debezium Oracle issues, mined OLR upstream issues and PRs, a code review of the Debezium 3.6.1 OLR adapter, and a code review of OLR 2.0.0. Entries that appear in several sources are one row with all links.

The lab runs Oracle 26ai Free (AL32UTF8, little-endian). Implemented scenarios run in two OLR output profiles, `debezium` (flags 0) and `json` (flags 32, SHOW_DDL), and are checked by run, diff (OLR vs LogMiner) and replay (before-snapshot + OLR events = after-snapshot). OLR 2.0.0, the fork v2.0.0-ahlsell.1 and 1.9.x run side by side; the Debezium maintainer says in OLR PRs 331/332 that Debezium is not compatible with 2.0 and wants fixes on 1.9.

Priority:
- P1: common column types (VARCHAR2/NUMBER/DATE) or a restart/data-loss path.
- P2: a plausible feature or an indirect path.
- P3: less common types or features (LOB, TIMESTAMP, CHAR, BINARY_*, XML, RAC).

The lab is little-endian AL32UTF8; big-endian hosts and single-byte database character sets are not covered.

Status:
- `implemented: <scenario>`: covered by `scenarios/<scenario>`.
- `partial: <scenario>`: part of the entry is covered; lab notes say what is missing.
- `planned: network mode`: needs more steps in the network client mode (`client.toml`, olrt/olr_net.py).
- `backlog`: can run in the current offline harness. `backlog (network mode)` and `backlog (dbz mode)` need the network client or a real Debezium Connect.
- `not testable in lab: <why>`.

Source keys: `catalog` = an Oracle redo edge-case catalog; `adapter review` = a code review of the Debezium 3.6.1 OLR adapter; `OLR review` = a code review of OLR 2.0.0; `harness finding` = found by this harness. Links are upstream issues and PRs.

## Types

| ID | What | Sources | Pri | Status | Lab notes |
|---|---|---|---|---|---|
| t-number-scale | NUMBER(p,s) and bare NUMBER: 0.000 vs 0, negatives, 38 digits, >15 significant digits, 1/3, FLOAT | catalog, https://github.com/debezium/dbz/issues/1681, https://issues.redhat.com/browse/DBZ-4240, https://issues.redhat.com/browse/DBZ-1552, https://issues.redhat.com/browse/DBZ-7882, adapter review, OLR review | P1 | implemented: types/number-scale | Scale restoration (0.000) happens in Debezium (withScaleAdjustedIfNeeded); Avro schema types need dbz mode |
| t-number-tiny | Very small magnitudes (1e-130 and up) | catalog | P2 | implemented: types/number-tiny |  |
| t-number-negative-scale | NUMBER(p,-s): dictionary load crash, value rounding | catalog, https://github.com/bersler/OpenLogReplicator/issues/329, https://github.com/bersler/OpenLogReplicator/pull/331, adapter review | P2 | implemented: types/number-negative-scale | Fix proposed only on 2.0 (PR 331); compare 1.9.x |
| t-number1-null-zero | NUMBER(1,0) with 0, 1, NULL; update to NULL | catalog, https://issues.redhat.com/browse/DBZ-3208 | P1 | implemented: types/number1-null-zero | Cheap; check first whether types/number-scale already covers NUMBER(1) |
| t-binary-float-double | BINARY_FLOAT/DOUBLE whole numbers, subnormals, NaN, +/-INF | catalog, https://github.com/debezium/dbz/issues/1679, https://github.com/bersler/OpenLogReplicator/pull/318 | P3 | implemented: types/binary-float-double | NaN/+-INF coverage not confirmed in the scenario |
| t-date-range | DATE/TIMESTAMP pre-1970, 0001-01-01, 9999-12-31, year 4247, years <1900 and >2262 | catalog, https://github.com/bersler/OpenLogReplicator/issues/171, https://issues.redhat.com/browse/DBZ-2784, OLR review | P1 | implemented: types/date-timestamp | OLR CHANGELOG 1.4.0 fix |
| t-date-bc | DATE before year 1 (BC) and 1582 Julian cutover | https://github.com/debezium/dbz/issues/1008, https://issues.redhat.com/browse/DBZ-8302, https://issues.redhat.com/browse/DBZ-7869 | P2 | backlog |  |
| t-date-dst-wall | DATE/TIMESTAMP wall-clock values in the DST gap and repeated hour; DB timezone != UTC | https://github.com/bersler/OpenLogReplicator/issues/321, https://issues.redhat.com/browse/DBZ-6143, https://issues.redhat.com/browse/DBZ-8889, https://issues.redhat.com/browse/DBZ-3268, https://issues.redhat.com/browse/DBZ-8379, adapter review, OLR review | P1 | partial: types/date-timestamp | DB timezone != UTC not covered; lab DB tz is container default |
| t-timestamp-fraction | TIMESTAMP(0..9) fractions | catalog | P3 | implemented: types/date-timestamp |  |
| t-tstz | TIMESTAMP WITH TIME ZONE, region and offset forms | catalog, https://issues.redhat.com/browse/DBZ-6143, https://issues.redhat.com/browse/DBZ-8889, adapter review | P3 | backlog | Debezium throws (non-retriable) unless OLR sends `<epochNanos>,<tz>`; that part needs dbz mode |
| t-tsltz | TIMESTAMP WITH LOCAL TIME ZONE with session TZ +05:30 and +10:00 | catalog, OLR review | P3 | backlog | parseTimezone is wrong for +10:00 and +05:30; +01:00/+02:00 happen to be right |
| t-interval | INTERVAL YEAR TO MONTH and DAY TO SECOND, negative and max-precision values | catalog, https://issues.redhat.com/browse/DBZ-6513, https://issues.redhat.com/browse/DBZ-7898, adapter review | P3 | backlog | Debezium expects interval-dts:9 / interval-ytm:4 string forms |
| t-char-padding | CHAR/NCHAR blank padding | catalog | P3 | implemented: types/char-padding |  |
| t-nvarchar | NCHAR/NVARCHAR2 in AL16UTF16 with non-Latin text, `\|\|`, quotes, newline | catalog, https://issues.redhat.com/browse/DBZ-9132 | P3 | partial: types/char-padding | NVARCHAR2 not covered |
| t-varchar-empty-null | '' vs NULL vs ' '; UPDATE setting only NULLs | catalog, https://issues.redhat.com/browse/DBZ-3631, https://issues.redhat.com/browse/DBZ-1211, https://github.com/debezium/dbz/issues/7 | P1 | implemented: types/varchar-empty-null |  |
| t-varchar-literals | VARCHAR2 values with `''`, `\|\|`, `;`, backslash, newline, trailing spaces, hex-like text, many literals in one wide UPDATE | implemented: types/varchar-literals-maxlen | P1 | backlog | LM parser bugs; in the diff check LogMiner is the side at risk, so a mismatch needs SELECT as tie-breaker |
| t-varchar-maxlen | VARCHAR2(4000) at full byte length, single- and multi-byte; VARCHAR2(32767) extended | https://issues.redhat.com/browse/DBZ-9392, https://issues.redhat.com/browse/DBZ-7018, catalog | P1 | implemented: types/varchar-literals-maxlen (4000 bytes; EXTENDED 32767 not tested) | Extended strings (MAX_STRING_SIZE=EXTENDED) are P3; 4000-byte part is P1 |
| t-raw | RAW with 0x00 bytes | catalog | P3 | backlog |  |
| t-rowid-column | Column of type ROWID (type 69) silently dropped | https://github.com/bersler/OpenLogReplicator/pull/319 | P3 | backlog | UROWID is supported |
| t-lob | CLOB/NCLOB/BLOB inline vs out-of-line, NULL vs empty, 1 B / 4 KB / 76 KB / 1 MB | catalog, https://issues.redhat.com/browse/DBZ-3257, https://issues.redhat.com/browse/DBZ-4366, https://issues.redhat.com/browse/DBZ-9392, https://issues.redhat.com/browse/DBZ-7018, https://issues.redhat.com/browse/DBZ-4853 | P3 | backlog | Absent LOB key becomes __debezium_unavailable_value in Debezium |
| t-lob-partial-write | DBMS_LOB.WRITE/WRITEAPPEND/TRIM/ERASE; same LOB updated several times in one txn | catalog, https://issues.redhat.com/browse/DBZ-4741, https://github.com/debezium/dbz/issues/579, https://github.com/debezium/dbz/issues/1634, https://github.com/debezium/dbz/issues/1900, https://github.com/debezium/dbz/issues/2368 | P3 | backlog |  |
| t-long | LONG / LONG RAW columns | catalog, https://issues.redhat.com/browse/DBZ-4853, https://issues.redhat.com/browse/DBZ-4880, https://issues.redhat.com/browse/DBZ-4852, OLR review | P3 | backlog | OLR omits LONG silently; Debezium then null-fills |
| t-xmltype | XMLTYPE CLOB and binary storage; repeated updates in one txn; column added online | catalog, https://issues.redhat.com/browse/DBZ-6896, https://issues.redhat.com/browse/DBZ-6782, https://issues.redhat.com/browse/DBZ-3605, https://github.com/debezium/dbz/issues/1373, https://github.com/debezium/dbz/issues/2159 | P3 | backlog |  |
| t-json-boolean | Native JSON and BOOLEAN (23ai+) | catalog, https://github.com/bersler/OpenLogReplicator/issues/242 | P3 | backlog | Do not exist in 19c; lab-only interest |
| t-identifiers | Quoted mixed-case and lowercase identifiers, reserved-word columns, names >30 chars | https://issues.redhat.com/browse/DBZ-4161, https://github.com/debezium/dbz/issues/1461, https://github.com/bersler/OpenLogReplicator/issues/274, adapter review | P2 | backlog | Debezium column lookup is exact-case; OLR filter is regex, lowercase fixed in 2.0.0 |

## Transactions and DML

| ID | What | Sources | Pri | Status | Lab notes |
|---|---|---|---|---|---|
| x-rollback | Full ROLLBACK; committed and rolled-back txns interleaved | catalog, https://issues.redhat.com/browse/DBZ-9074 | P1 | implemented: txn/rollback |  |
| x-rollback-large | Rolled-back txn with 300k rows across several logs: no events, position advances | https://github.com/debezium/dbz/issues/1145, https://issues.redhat.com/browse/DBZ-9686 | P1 | partial: txn/rollback | Small txn only |
| x-savepoint | ROLLBACK TO SAVEPOINT; same row before/after savepoint; several savepoints in a batch | catalog, https://github.com/debezium/dbz/issues/1422, https://github.com/debezium/dbz/issues/1735, https://issues.redhat.com/browse/DBZ-9615, https://github.com/debezium/dbz/issues/1914, https://github.com/debezium/dbz/issues/1917, https://github.com/debezium/dbz/issues/2531, https://github.com/debezium/dbz/issues/2634, https://github.com/debezium/dbz/issues/2666 | P1 | implemented: txn/savepoint-partial-rollback | LOB variants not covered (P3) |
| x-partial-rollback-wide | Partial rollback on multi-piece (>255 col) rows, 0x0B10 pieces, WARN 70003 | OLR review | P1 | implemented: txn/partial-rollback-wide-row |  |
| x-stmt-rollback | Statement-level rollback: multi-row INSERT/UPDATE hits a constraint violation (PK dup, FK missing) mid-statement, txn then commits other rows | catalog, OLR review | P1 | implemented: txn/stmt-rollback (narrow; wide variant is tests/adversarial) | Second WARN 70003 path; on wide and narrow tables |
| x-same-row-multi-op | Same row INSERT, UPDATE, UPDATE, DELETE in one txn and across txns; double DELETE attempt in one XID | https://github.com/bersler/OpenLogReplicator/issues/10, https://issues.redhat.com/browse/DBZ-5656, https://issues.redhat.com/browse/DBZ-5750, https://issues.redhat.com/browse/DBZ-6963, https://github.com/debezium/dbz/issues/2184, https://issues.redhat.com/browse/DBZ-5945 | P1 | implemented: txn/same-row-multi-op |  |
| x-interleaved | T1 begins, T2 begins and commits, T1 commits later | catalog, https://github.com/debezium/dbz/issues/2307, https://github.com/bersler/OpenLogReplicator/issues/330 | P1 | implemented: txn/interleaved | Restart between commits is r-long-open-client-restart |
| x-large | Single txn with many rows | catalog, https://github.com/debezium/dbz/issues/1425, https://github.com/debezium/dbz/issues/1099, https://github.com/bersler/OpenLogReplicator/issues/24 | P1 | implemented: txn/large | 60k rows; 300k via network writer is r-queue-deadlock |
| x-txn-swap | Txn larger than memory.max-mb so OLR swaps to disk; output identical to no-swap run | OLR review, OLR CHANGELOG 1.8.0 (PR 186 swap bug) | P1 | backlog | Run txn/large with memory.max-mb set low; also checks the memory stall that logs nothing |
| x-transaction-max-mb | Txn above transaction-max-mb is dropped with only a trace log | OLR review | P2 | backlog | Documents behaviour; default is 0 |
| x-long-open-many-logs | Txn open across many log switches and archive logs, other txns committing meanwhile | catalog, https://github.com/debezium/dbz/issues/1390, https://github.com/debezium/dbz/issues/1353, https://issues.redhat.com/browse/DBZ-9552, https://issues.redhat.com/browse/DBZ-9445, https://github.com/debezium/dbz/issues/97 | P1 | partial: txn/multi-logswitch | LM retention abandonment has no OLR equivalent; check that OLR keeps it |
| x-multirow-dml | Multi-row INSERT, INSERT ALL, INSERT SELECT, MERGE, multi-row UPDATE/DELETE | catalog | P1 | implemented: txn/multirow-dml |  |
| x-forall | FORALL array DML (UPDATE/INSERT/DELETE) with INDICES OF | catalog | P2 | implemented: txn/forall-array-dml | Array inserts may produce QMI (11.11) records like INSERT SELECT |
| x-update-pk | UPDATE of the PK column | catalog | P1 | implemented: txn/update-pk |  |
| x-noop-update | SET col = col (no-op) updates | catalog | P1 | implemented: wide/v06-noop-all | Also wide/v02-noop-shift, wide/v05-noop-high |
| x-trailing-nulls | Narrow table: INSERT/DELETE of rows ending in NULLs (short row encoding) | catalog | P1 | implemented: txn/trailing-nulls | Covered for wide rows (50073 family), not for a narrow table |
| x-lock-row | SELECT FOR UPDATE, ROWDEPENDENCIES, BEFORE ROW trigger (op 5.1 LKR, ERROR 50061) | catalog, https://github.com/bersler/OpenLogReplicator/issues/162, https://github.com/bersler/OpenLogReplicator/pull/228, OLR review | P1 | implemented: txn/lock-row-trigger | Wide variant: wide/select-for-update-313 |
| x-no-pk | Table without PK: supp log group, unique-index-only | https://issues.redhat.com/browse/DBZ-3631, https://issues.redhat.com/browse/DBZ-1211, https://github.com/debezium/dbz/issues/7 | P1 | implemented: txn/no-pk-table |  |
| x-autonomous | Autonomous txn committing inside an open outer txn | catalog | P2 | implemented: txn/autonomous-nested |  |
| x-xa | XA / distributed txn: prepare, commit from another session, rollback of prepared branch | catalog | P2 | implemented: txn/xa-dbms-xa | Testable in Free with DBMS_XA, no RAC needed |
| x-overlapping-xid | Many sessions with short txns so undo slots / XIDs are reused; overlapping starts | https://github.com/bersler/OpenLogReplicator/issues/251, https://github.com/bersler/OpenLogReplicator/pull/315 | P2 | backlog | Fixed in 2.0.0 for PDB case; non-PDB not verified |

## DDL

| ID | What | Sources | Pri | Status | Lab notes |
|---|---|---|---|---|---|
| d-add-column | ALTER TABLE ADD column then DML | catalog, https://issues.redhat.com/browse/DBZ-7952, https://github.com/debezium/dbz/issues/992, https://github.com/debezium/dbz/issues/2404, https://github.com/debezium/dbz/issues/2405, https://github.com/debezium/dbz/issues/1348, https://issues.redhat.com/browse/DBZ-6677, OLR review | P1 | implemented: ddl/add-column-then-dml |  |
| d-add-column-fast-default | ADD column DEFAULT x NOT NULL (metadata-only add), then UPDATE/DELETE of rows that existed before the add | https://issues.redhat.com/browse/DBZ-7952, OLR CHANGELOG 1.6.0 (PR 120), OLR review (WARN 60034) | P1 | implemented: ddl/add-column-fast-default | Old rows have no value in redo for the new column; risk of NULL in a NOT NULL column, which Debezium turns into ""/0 |
| d-add-column-wide | ADD column on a 313-column table mid-stream, then DML touching high columns | https://github.com/debezium/dbz/issues/1283 | P1 | implemented: ddl/add-column-fast-default (312 -> 313 columns) | Combine wide/v01-repro schema with ddl/add-column-then-dml |
| d-ddl-in-debezium-profile | DDL events in the debezium profile (flags 0) vs json profile (flags 32 SHOW_DDL); does Debezium learn about the new column | OLR review, adapter review | P1 | implemented: connect/ddl-add-column, connect/truncate*, connect/ddl-move | Run/diff pass in both profiles; whether Debezium applies the schema change needs dbz mode |
| d-drop-column | DROP COLUMN (physical) and DROP UNUSED COLUMNS, then DML; segcol renumbering | catalog, https://github.com/debezium/dbz/issues/1283, adapter review, OLR review | P1 | implemented: ddl/drop-column-then-dml |  |
| d-modify-column | MODIFY VARCHAR2 width and NUMBER precision/scale, then DML with values only valid after the change | catalog, adapter review | P1 | implemented: ddl/modify-column-then-dml | Value wider than Debezium's stale schema fails or nulls |
| d-rename-column | RENAME COLUMN then DML | catalog, adapter review | P2 | implemented: ddl/rename-column |  |
| d-set-unused-default-identity | SET UNUSED, DEFAULT ON NULL, identity columns | catalog, OLR review | P1 | implemented: ddl/set-unused-default-identity |  |
| d-truncate | TRUNCATE then DML | catalog, https://issues.redhat.com/browse/DBZ-4953, https://issues.redhat.com/browse/DBZ-4385, https://issues.redhat.com/browse/DBZ-7242, https://issues.redhat.com/browse/DBZ-4017 | P1 | implemented: ddl/truncate-then-dml | TRUNCATE PARTITION and cluster in s-partition-ddl / s-cluster |
| d-move | ALTER TABLE MOVE (dataobj != obj), DML after | catalog, OLR review, https://github.com/debezium/dbz/issues/2169 | P1 | implemented: ddl/move-mid-stream | Also wide/v10-nomove, wide/v11-nomove-shift |
| d-extended-stats | Extended statistics create/drop (hidden virtual columns, segcol 0) | catalog, OLR review | P1 | implemented: ddl/extended-stats-mid-stream | Shifted-column wide variants: wide/v04-date-shift, wide/v11-nomove-shift |
| d-create-table-after-start | CREATE TABLE in captured schema after OLR start, then DML | https://github.com/debezium/dbz/issues/1480 | P1 | implemented: ddl/create-table-after-start |  |
| d-supp-log-change | Change supplemental logging on a captured table mid-stream (ALL -> PK only -> none -> ALL), UPDATE one column of a wide row each time | catalog, https://issues.redhat.com/browse/DBZ-4869, https://issues.redhat.com/browse/DBZ-7341, https://issues.redhat.com/browse/DBZ-3521, adapter review, OLR review | P1 | implemented: ddl/supp-log-change | Expect partial before images; replay check catches it. Application upgrades that drop supplemental logging hit this path |
| d-drop-rename-table | DROP TABLE to recycle bin (BIN$), FLASHBACK TO BEFORE DROP, RENAME table, RENAME CONSTRAINT | catalog, https://github.com/debezium/dbz/issues/886, https://issues.redhat.com/browse/DBZ-6897 | P2 | partial: ddl/rename-table |  |
| d-drop-pk | DROP PRIMARY KEY / add PK, then DML | https://issues.redhat.com/browse/DBZ-9505 | P2 | implemented: ddl/drop-pk-then-dml | Key schema change is a dbz-mode check |
| d-default-expr | Column DEFAULT (0.000) in parentheses, INTERVAL default; insert omitting the column | https://github.com/debezium/dbz/issues/16, https://issues.redhat.com/browse/DBZ-7898 | P2 | implemented: ddl/default-expr | Debezium DDL parser issue; dbz mode |
| d-ddl-long | DDL text > 4000 chars (CREATE of a 313-column table mid-stream) | https://github.com/bersler/OpenLogReplicator/issues/104, https://github.com/bersler/OpenLogReplicator/issues/198 | P2 | backlog | Fixed in 1.8.0; cheap regression |
| d-ddl-fk-name | DDL with FK reference: table name in payload vs SQL | https://github.com/bersler/OpenLogReplicator/issues/244 | P3 | backlog |  |
| d-flashback-table | FLASHBACK TABLE TO SCN (generated DML) | catalog | P3 | backlog |  |
| d-redefinition | DBMS_REDEFINITION of a captured table, then DML | https://github.com/debezium/dbz/issues/1588 | P3 | backlog |  |

## Storage and row format

| ID | What | Sources | Pri | Status | Lab notes |
|---|---|---|---|---|---|
| s-wide-rows | Multi-piece rows >255 columns: no-op updates, before/after images, delete vs update, full-row variants, split txn | catalog, OLR review, OLR CHANGELOG 1.6.0 (PR 128, PR 131), OLR CHANGELOG 1.7.0 (PR 157) | P1 | implemented: wide/v01-repro .. wide/v19-split-txn | ERROR 50073 family |
| s-chained-migrated | Chained and migrated rows (PCTFREE 0, row growth), WARN 60017 row piece mismatch | catalog, OLR review | P1 | implemented: wide/v12-chained, storage/chained-pctfree0 | Also wide/v13-chained-shift. Narrow-table migration via VARCHAR2 growth not separate |
| s-basic-compression | COMPRESS BASIC + APPEND + multi-row insert | catalog, https://github.com/bersler/OpenLogReplicator/issues/323 | P2 | implemented: storage/basic-compression |  |
| s-advanced-compression | ROW STORE COMPRESS ADVANCED with QMI bulk insert (ERROR 50009) | catalog, https://github.com/bersler/OpenLogReplicator/issues/323 | P2 | implemented: storage/oltp-compression (xfail: OLR does not decode OLTP-compressed row pieces) | Repro SQL is in issue 323 |
| s-nologging-direct-path | NOLOGGING + direct-path INSERT, CTAS | catalog, OLR review | P2 | implemented: storage/nologging-direct-path |  |
| s-partition-row-move | UPDATE of partition key with ENABLE ROW MOVEMENT (delete+insert in redo) | catalog, https://issues.redhat.com/browse/DBZ-2683, https://issues.redhat.com/browse/DBZ-2841 | P2 | implemented: storage/partition-row-move |  |
| s-subpartition | Composite RANGE+HASH subpartition DML | catalog, OLR CHANGELOG 1.9.0 (PR 250, PR 253) | P2 | backlog |  |
| s-partition-ddl | SPLIT/MOVE/RENAME/TRUNCATE/EXCHANGE PARTITION, interval partition auto-create, then DML | catalog, https://issues.redhat.com/browse/DBZ-9534, https://issues.redhat.com/browse/DBZ-9651, https://issues.redhat.com/browse/DBZ-9238, https://github.com/debezium/dbz/issues/2408, https://issues.redhat.com/browse/DBZ-4953 | P2 | backlog |  |
| s-virtual-column | Virtual column (also NOT NULL expression) in a captured table | catalog, https://github.com/debezium/dbz/issues/1362, adapter review | P2 | implemented: storage/virtual-invisible-columns | Redo has no value; Debezium knows the column from JDBC and null-fills |
| s-invisible-column | INVISIBLE column, DML touching it | catalog, https://github.com/debezium/dbz/issues/2404, https://github.com/debezium/dbz/issues/2405 | P2 | implemented: storage/virtual-invisible-columns |  |
| s-hidden-columns | ROW ARCHIVAL (ORA_ARCHIVE_STATE) and other hidden columns | catalog, https://github.com/debezium/dbz/issues/1676, https://github.com/debezium/dbz/issues/1650, https://github.com/debezium/dbz/issues/1774 | P3 | backlog | Only if used |
| s-iot | Index-organized table | catalog, OLR review | P3 | backlog | OLR skips IOTs at schema build; expect clean skip |
| s-cluster | Clustered tables, TRUNCATE CLUSTER | catalog, https://issues.redhat.com/browse/DBZ-4017 | P3 | backlog |  |
| s-rowid | source.row_id / OLR rid vs SELECT ROWID across datafiles, bigfile tablespace, after MOVE | https://github.com/debezium/dbz/issues/2169 | P2 | backlog | rid comparison can be a harness check; row_id encoding is Debezium side |
| s-stored-generated | STORED generated column | catalog | P3 | not testable in lab: Oracle has only VIRTUAL generated columns |  |
| s-tde | Encrypted tablespace (TDE) | OLR CHANGELOG 1.8.6 (PR 235, PR 236) | P3 | backlog | Only if TDE is used |

## Restart, protocol and event metadata

| ID | What | Sources | Pri | Status | Lab notes |
|---|---|---|---|---|---|
| r-long-open-client-restart | Long txn open while the client restarts (CONTINUE); c_scn = begin SCN filters it | OLR review, https://github.com/bersler/OpenLogReplicator/issues/330, https://github.com/debezium/dbz/issues/2307 | P1 | implemented: txn/long-open-across-client-restart (+ txn/long-open-control) | Begin-SCN `c_scn` stamping; fixed by fork `fix/commit-scn-stamping`. START variant: txn/long-open-across-olr-restart |
| r-cold-start-mid-txn | OLR cold start (no checkpoint) while a txn is open; also txn open at Debezium snapshot time (WARN 60011) | OLR review, adapter review | P1 | implemented: txn/cold-start-mid-transaction | Without `fix/cold-start-low-watermark` OLR logs WARN 60011 skipping transaction with no beginning and loses rows |
| r-restart-mid-large-txn | Client SIGKILL in the middle of a large txn; count rows per txId vs source | https://github.com/debezium/dbz/issues/2544, https://issues.redhat.com/browse/DBZ-6895, adapter review | P1 | implemented: adv/net-continue-mid-txn, adv/net-kill-mid-big-txn, connect/restart-task-mid-txn | Repeat with OLR restarted at the same time (READY branch drops c_idx) |
| r-confirm-ahead | Confirm of received-but-not-written events, then client death | adapter review | P1 | implemented: connect/restart-task-mid-txn, connect/worker-kill-mid-txn (3.6.1 loses rows, 3.7.0 passes; fork WARN 60039) | Also the zombie: task RUNNING but not streaming after handshake refusal (dbz mode) |
| r-olr-restart | OLR restart while idle and while streaming; checkpoint load after kill -9 | adapter review, https://github.com/bersler/OpenLogReplicator/issues/243, https://github.com/bersler/OpenLogReplicator/issues/113, https://github.com/bersler/OpenLogReplicator/issues/116, https://github.com/bersler/OpenLogReplicator/issues/40 | P1 | implemented: txn/long-open-across-olr-restart, adv/net-restart-*, restart/*, connect/olr-restart* | Clean close is non-retriable in Debezium 3.6.1 ("Connection lost"); measure restart time for issue 40 |
| r-reconnect-loss | Client stops mid-batch and resumes with CONTINUE; off-by-one; older position than last received; scn-only CONTINUE | https://github.com/bersler/OpenLogReplicator/issues/324, https://github.com/bersler/OpenLogReplicator/issues/325, https://github.com/bersler/OpenLogReplicator/issues/227, https://github.com/bersler/OpenLogReplicator/pull/332 | P1 | implemented: adv/net-continue-mid-txn (fixed in the fork: c_idx) | PR 332 open on 2.0; maintainer wants fixes on 1.9 (PR 331/332 comments) |
| r-queue-deadlock | Txn larger than network writer queue-size: deadlock; backpressure from a slow client | https://github.com/bersler/OpenLogReplicator/issues/24, adapter review | P1 | implemented: adv/net-small-queue (xfail; WARN 60040, operational limit queue-size >= largest transaction) | Debezium only confirms when prevScn < newScn, so nothing is released mid-txn |
| r-random-kill | Kill client repeatedly at random points during sustained small txns; each txn exactly once | https://github.com/debezium/dbz/issues/66, https://issues.redhat.com/browse/DBZ-8760, https://issues.redhat.com/browse/DBZ-4936, https://issues.redhat.com/browse/DBZ-4737, https://github.com/debezium/dbz/issues/9, https://github.com/bersler/OpenLogReplicator/issues/58 | P1 | partial: adv/net-kill-mid-big-txn, restart/switch-after-kill-* |  |
| r-resume-without-idx | Resume with offset lacking scn_idx (START scn only) | https://github.com/debezium/dbz/issues/1756, adapter review | P1 | backlog (network mode) |  |
| r-commit-timestamp | Redo/commit timestamps (tm, source.ts_ms) vs real UTC, around DST | harness finding, OLR review, adapter review, https://github.com/bersler/OpenLogReplicator/issues/321 | P1 | implemented: meta/commit-timestamp | Without reader `host-timezone` OLR emits the DB host's wall time as UTC. DST variant: meta/commit-timestamp with profile `debezium-tz`, meta/commit-timestamp-host-tz-iana (fork `fix/host-timezone-iana`) |
| r-column-omission | Every non-virtual column present in after/before for every event; NOT NULL column missing becomes ""/0/0L in Debezium | adapter review, OLR review | P1 | implemented: connect/missing-column | Payload completeness can be a harness check in run mode; the ""/0 substitution itself needs dbz mode |
| r-format-compat | Debezium 3.6.1 handshake and field contract vs OLR 2.0.0, fork, 1.9.x (xid format, c_scn/c_idx, scn-type, tm units, db field, commit_scn empty) | https://github.com/bersler/OpenLogReplicator/issues/184, https://github.com/bersler/OpenLogReplicator/issues/280, https://github.com/bersler/OpenLogReplicator/pull/331, https://github.com/bersler/OpenLogReplicator/pull/332, https://github.com/bersler/OpenLogReplicator/pull/302, https://github.com/bersler/OpenLogReplicator/pull/309, adapter review | P1 | partial: connect/smoke (Debezium 3.6.1 and 3.7.0 vs fork b631f01d) | Harness runs 2.0.0, v2.0.0-ahlsell.1 and 1.9.x side by side; Debezium maintainer: 3.6 not compatible with 2.0 |
| r-idle-heartbeat | Idle captured tables: offsets only advance with heartbeat.interval.ms; pause/stop/signal wait for next event | adapter review, https://github.com/debezium/dbz/issues/1429, https://issues.redhat.com/browse/DBZ-9654 | P1 | backlog (dbz mode) |  |
| r-first-event | First DML right after connector start / after snapshot is delivered | https://issues.redhat.com/browse/DBZ-8141 | P2 | backlog (dbz mode) |  |
| r-snapshot-scn-equal | Txn with commit SCN == snapshot SCN appears in both snapshot and stream | adapter review | P2 | backlog (dbz mode) |  |
| r-graceful-shutdown | Stop client and OLR during streaming (SIGTERM); no gap, no dup; 2.0.0 immediate shutdown with txn in flight | https://github.com/debezium/dbz/issues/2299, OLR CHANGELOG 2.0.0 | P2 | implemented: meta/olr-sigterm |  |
| r-incremental-snapshot | Blocking and incremental snapshot signals during DML load; signal table must be in OLR filter | https://github.com/debezium/dbz/issues/2301, https://issues.redhat.com/browse/DBZ-4404, https://issues.redhat.com/browse/DBZ-5943, https://issues.redhat.com/browse/DBZ-7886, https://github.com/debezium/dbz/issues/1108, adapter review | P2 | backlog (dbz mode) | Chunk reads query the source by design |
| r-equal-commit-scn | Two txns with the same commit SCN: confirm delay | adapter review | P3 | backlog (dbz mode) | Hard to force |

## Charset and byte order

| ID | What | Sources | Pri | Status | Lab notes |
|---|---|---|---|---|---|
| c-latin1-high | WE8ISO8859P1: every byte 0x80-0xFF (incl. C1 range 0x80-0x9F), chars outside Latin-1 (EUR sign) converted on insert | https://github.com/debezium/dbz/issues/1798, OLR review | P1 | partial: types/nonascii | Lab DB is AL32UTF8. WE8ISO8859P1 part needs a WE8ISO8859P1 database or a redo corpus from one |
| c-lob-single-byte | CLOB in a single-byte DB charset decoded as two-byte | https://github.com/bersler/OpenLogReplicator/issues/114 | P2 | not testable in lab: needs WE8ISO8859P1 DB | Redo corpus from a single-byte database |
| c-big-endian-redo | All opcode paths on big-endian redo | OLR review | P1 | not testable in lab: needs a big-endian host (e.g. AIX) | Needs a big-endian redo corpus; no upstream big-endian reports either way |
| c-xid-byte-order | source.txId byte-swapped on big-endian hosts (OLR vs LogMiner XID format) | OLR review | P1 | partial: meta/xid-logminer (little-endian only; big-endian not testable in lab) | Needs a big-endian redo corpus; consumers comparing txId need to know |
| c-network-endian | Network writer length prefix sent in host order | OLR review, OLR CHANGELOG 2.0.0 (PR 291) | P3 | not testable in lab: both ends little-endian | Only if OLR ever runs on a big-endian host |

## Operational and infrastructure

| ID | What | Sources | Pri | Status | Lab notes |
|---|---|---|---|---|---|
| o-archive-gap | Archived log deleted or renamed before OLR reads it: expect hard stop with non-zero exit, never a silent skip | catalog, https://github.com/debezium/dbz/issues/2504, https://issues.redhat.com/browse/DBZ-3256, https://issues.redhat.com/browse/DBZ-3266, https://issues.redhat.com/browse/DBZ-5256, https://github.com/bersler/OpenLogReplicator/issues/89, OLR review | P1 | implemented: ops/archive-gap, archive/* | Reproducible in lab: delete/rename an archived log |
| o-logswitch-burst | 20+ forced log switches during a DML burst; rows per table equal | catalog, https://issues.redhat.com/browse/DBZ-3295, https://issues.redhat.com/browse/DBZ-6679, https://issues.redhat.com/browse/DBZ-3319, https://github.com/debezium/dbz/issues/1374 | P1 | partial: txn/multi-logswitch |  |
| o-online-overwritten | Online log overwritten before OLR read it: continue from archive; online log read while being written | OLR review, OLR CHANGELOG 1.7.0 (PR 158) | P1 | backlog | Verified manually in lab, no scenario yet; needs small online logs and a paused reader |
| o-checkpoint-retention | Checkpoint count vs interval-s and interval-mb (200 files = 24 h, not 100 h) | OLR review | P1 | implemented: state/checkpoint-every-loop, state/checkpoint-interval-mb-large | Config regression check; set interval-mb 0 and assert time-only trigger |
| o-resume-from-archives | OLR down longer than online redo wrap, restart catches up from archives; archive dir rescan | https://github.com/bersler/OpenLogReplicator/issues/220, https://github.com/bersler/OpenLogReplicator/issues/334, https://github.com/bersler/OpenLogReplicator/issues/40 | P1 | backlog (network mode) |  |
| o-nfs-eio | NFS soft-mount EIO and stale st_size on a just-archived log | OLR review, https://github.com/bersler/OpenLogReplicator/issues/166, https://github.com/bersler/OpenLogReplicator/issues/218 | P1 | implemented: archive/eio-mid-read, archive/estale-mid-read (LD_PRELOAD pread shim, no NFS needed) | A real NFS outage (block port 2049) needs a host with an NFS mount; half-copied archive part can be done in lab, see o-truncated-archive |
| o-truncated-archive | Archive log file shorter than its header nab (half-copied) in batch mode | OLR review | P2 | implemented: archive/truncate-before-open, archive/truncate-mid-read | Copy of a lab archive log truncated mid-file |
| o-exit-code | Fatal error (missing grant, ERROR 10034) exits non-zero and does not spin CPU | https://github.com/bersler/OpenLogReplicator/issues/328 | P2 | implemented: meta/olr-sigterm, archive/* (gap check requires a non-zero exit) | Upstream 2.0.0 exits rc 0 on fatal error and ignores SIGTERM as PID 1 (fork `fix/exit-code-on-error`) |
| o-undo-too-old | Start with offset older than undo retention / oldest checkpoint (ORA-01555) | catalog | P2 | backlog (network mode) |  |
| o-checkpoint-kill | SIGKILL while OLR writes a 44 MB checkpoint, then restart | https://github.com/bersler/OpenLogReplicator/issues/243 | P2 | implemented: restart/chkpt-in-commit-lwn-*, restart/switch-after-kill-* |  |
| o-pdb-dbid | Plugged PDB where V$PDBS.DBID != CON_UID: all txns silently dropped | https://github.com/bersler/OpenLogReplicator/issues/333 | P3 | implemented: meta/root-container-cold-start, meta/pdb-container-cold-start-control | Only if a PDB is replicated; lab PDB needs unplug/plug |
| o-redo-corruption | Corrupted redo block (checksum) on a copied redo file; transient CRC on online log with redo-verify-delay-us | catalog, OLR review | P3 | partial: archive/zero-mid-read (zeroed read, not corrupted content) | Corrupt a copy offline; expect clear error |
| o-tablespace-undo-ops | Tablespace OFFLINE/ONLINE, UNDO tablespace switch during DML | catalog | P3 | backlog |  |
| o-rac-asm | RAC threads, duplicate SCNs per thread, ASM | https://issues.redhat.com/browse/DBZ-5439, https://issues.redhat.com/browse/DBZ-5245, https://issues.redhat.com/browse/DBZ-3563, https://github.com/debezium/dbz/issues/1801, https://github.com/debezium/dbz/issues/2049, https://github.com/debezium/dbz/issues/1767, https://issues.redhat.com/browse/DBZ-8724, https://github.com/debezium/dbz/issues/1589, https://github.com/debezium/dbz/issues/1590, https://github.com/bersler/OpenLogReplicator/issues/191 | P3 | not testable in lab: RAC/ASM not in Free and not supported by OLR |  |
| o-block-corruption | Datafile block corruption | catalog | P3 | not testable in lab: OLR reads redo only |  |
| o-archive-only-mode | LM archive.log.only.mode skipping online-redo rows | https://github.com/debezium/dbz/issues/2296, https://issues.redhat.com/browse/DBZ-8345, https://github.com/debezium/dbz/issues/2605 | P3 | implemented: meta/arch-only-cold-start (flags 1; fixed in the fork) |  |
## Still open

The most valuable gaps:

1. `r-resume-without-idx`, `r-idle-heartbeat`, `r-incremental-snapshot` (Connect mode; the connector runs, the scenarios do not exist yet).
2. `o-online-overwritten`, `o-resume-from-archives`, `o-undo-too-old` (restart paths that depend on the online log or undo being gone).
3. `x-txn-swap` and `x-transaction-max-mb`: `txn/large` with a low `memory.max-mb` to exercise the swap path.
4. `c-latin1-high`: needs redo from a WE8ISO8859P1 database (a big-endian source also unblocks `c-big-endian-redo` and `c-xid-byte-order`).
5. OLR limitations the suite documents but the fork does not fix: OLTP compression decode, direct-path rows (WARN 60042 only), a transaction larger than `queue-size`.
6. Not covered: a commit before a cold start with an older start SCN (builds with `fix/cold-start-low-watermark` are expected to log ERROR 60011, rows lost), and the DST switch itself, which the fork's TimeZoneTest unit test covers and a lab clock cannot reproduce.
