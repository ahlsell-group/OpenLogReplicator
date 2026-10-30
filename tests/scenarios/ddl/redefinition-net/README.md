Network writer with a Debezium-like client: a layout-changing `DBMS_REDEFINITION` (TXT and D move before
QTY, wider types, EXTRA new) with OLR restarted twice, once between START and FINISH and once after the
swap, the client reconnecting with CONTINUE each time. On the second restart OLR reads the dictionary as of
its restart SCN, which is before FINISH (the old object), and has to follow the swap from redo again.
LogMiner uses the redo dictionary here: with the online catalog it attributes the 20 000-row copy into the
interim table to T, because the interim's object is named T by the time it mines.
