OLR is killed and started five times, 15 s apart (Debezium waits retriable.restart.connector.wait.ms = 10 s
before reconnecting; writer checkpoint deleted each time, as an init container might do). Connector:
`internal.custom.retriable.exception = .*Connection lost.*`, `errors.max.retries = 3`.
Expected: on 3.6.1 every reconnect re-sends the boundary transaction (START is inclusive),
that duplicate counts as change data, Debezium resets its retry counter, and the loop runs forever with one
duplicate per cycle (task RUNNING). On 3.7.0 the duplicate is discarded, the counter is never reset, and the
task ends FAILED after the third reconnect.
