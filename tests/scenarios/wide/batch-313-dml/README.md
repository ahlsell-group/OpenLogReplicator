Source tables with up to 313 columns often take big batch transactions. Combines the multi-row DML forms (INSERT ALL, MERGE, FORALL, INSERT SELECT) with row pieces spanning more than 255 columns.

Step 6 (`UPDATE ... SET c3 = ''`) is the only '' -> NULL case on a multi-piece row in the suite; keep it even though types/varchar-empty-null covers the narrow case.
