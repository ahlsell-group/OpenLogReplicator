With ALL COLUMNS supplemental logging, the lock-row undo of a 313-column
row may span several undo blocks; if OLR does not store the paired op it keeps a dangling
split and stops with ERROR 50041 bad split offset. Rows
251/1251 are not used by other wide scenarios.
