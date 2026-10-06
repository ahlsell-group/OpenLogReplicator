Deterministic form of archive/truncate-mid-read: once OLR has started on the victim, pread returns 0 (end of file) at any offset, as if the file had been truncated to the reader's position. Safe: ERROR and non-zero exit, no row after the gap.

Without fork fix/archive-short-read OLR delivers the rows before the damage, then rows from the next sequence, exit 0: a silent gap. With fix/archive-short-read it stops with ERROR 40013, then ERROR 10047, and exit 1, nothing after the gap.
