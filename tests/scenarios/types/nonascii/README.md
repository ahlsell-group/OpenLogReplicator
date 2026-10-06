UTF-8 round trip of non-ASCII text (Nordic letters, euro sign, CJK, emoji, combining marks). The lab database
is AL32UTF8, so the 0x80-0x9F CP1252 range of a WE8ISO8859P1 database is only covered by
a replay of redo from such a database (see README, limitations).
