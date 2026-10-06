MOVE gives the segment a new dataobj#. A long-lived table such as WIDE_278 (wide/) has dataobj != obj; here the change
happens live and OLR must map redo for the new dataobj to the same table.
