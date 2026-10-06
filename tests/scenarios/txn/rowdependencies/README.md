Isolates one ingredient of txn/lock-row-trigger: ROWDEPENDENCIES adds a 6-byte row SCN to
every row header, which changes the row-piece layout OLR parses (upstream #162/#228).
