`DBMS_REDEFINITION` rebuilds the table into an interim table (here only PCTFREE/INITRANS differ) and
`FINISH_REDEF_TABLE` swaps the two: after it the name T belongs to the interim's object, with another obj#
and dataobj#, and T_INT to the old one. Only T is captured. DML runs during the copy in `START_REDEF_TABLE`
(a scheduler job of small transactions; `MARKS` has the timestamps), between START and SYNC, between SYNC
and FINISH, and after the swap on rows that existed before it, then the old table is dropped.
OLR must follow the swap from the dictionary redo, and the copy into the interim table (direct path, not
captured) must not appear as changes to T. LogMiner (redo dictionary) is the reference.
