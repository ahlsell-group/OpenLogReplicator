A transaction whose redo is spread over four archived logs. OLR must stitch it together
across sequences; a short transaction committing in between must not be delayed or merged.
