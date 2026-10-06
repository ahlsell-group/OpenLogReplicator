The filter names a table that does not exist when OLR reads the dictionary. OLR must pick
it up from the CREATE TABLE in redo (Debezium issues 1480/886).
