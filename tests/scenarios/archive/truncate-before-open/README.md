The archived log is truncated to 60 % before OLR starts (V$ARCHIVED_LOG still lists it with its full size, and its header still says how many blocks it has). Safe: ERROR and non-zero exit. Unsafe: OLR reads up to the short size and moves on.

Without fork fix/archive-short-read OLR delivers the rows before the damage, then rows from the next sequence, exit 0: a silent gap. With fix/archive-short-read it stops with ERROR 40012 (retried), then ERROR 10009, and exit 1, nothing after the gap.
