DATE and TIMESTAMP values arrive as the stored wall time. No shift by the database time zone
(the container runs in Europe/Berlin to make a shift visible), no loss of nanoseconds, no
overflow for years before 1970 or after 2262.

One create event per inserted row, 3 updates (ids 1, 4, 100), 1 delete (id 2). After replay
every date and timestamp column equals the database value, to the nanosecond.
