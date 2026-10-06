Oracle stores '' as NULL. OLR must emit null, not "". A single blank is a real value.
Also an UPDATE that only sets NULLs (Debezium issue list: update with only NULL values).
