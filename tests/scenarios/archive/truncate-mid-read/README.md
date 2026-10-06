The archived log shrinks (truncate -s to a block-aligned 60 %) after OLR opened it and before the reader got there. pread then returns 0 bytes at the old offsets. Safe: ERROR and non-zero exit, no row after the gap. Unsafe: OLR treats the short file as complete and moves on to the next sequence.

Without fork fix/archive-short-read OLR delivers the rows before the damage, then rows from the next sequence, exit 0: a silent gap. With fix/archive-short-read it stops with ERROR 40013, then ERROR 10047, and exit 1, nothing after the gap.
