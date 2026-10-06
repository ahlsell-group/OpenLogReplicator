Every row of `WIDE_C` is stored in two row pieces (columns up to 256, then the rest). Expected
output is one event per changed row, with the changed columns and the unchanged ones present in
the after-image:

- ids 1..5: single-piece and two-piece updates, NULL transitions, a no-op `SET c = c`
- ids 10..60: 51 updates that grow the row until it migrates to another block
- id 20: one more update after the migration
- ids 1, 15 and 99 deleted, id 500 inserted with only a few columns set (the others NULL)

Event counts are not asserted: whether the no-op update and the migrated rows produce one or
more events depends on Oracle's redo, the replay check covers the end state.
