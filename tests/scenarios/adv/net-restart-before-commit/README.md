Batch transactions can stay open for hours over many log switches. Here a 5 000-row transaction (narrow
inserts, two-piece 300-column rows, updates of earlier rows) spans three log switches while short
transactions commit and are confirmed; OLR is killed right before the COMMIT and restarts from its own
checkpoint (CONTINUE path). OLR must rebuild the open transaction from its checkpoint/redo and deliver it
once, complete.
