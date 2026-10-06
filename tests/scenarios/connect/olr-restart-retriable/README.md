Same as olr-restart without the manual task restart, with `internal.custom.retriable.exception = .*Connection lost.*`
(the real property name of Debezium's custom retriable pattern, `CommonConnectorConfig.CUSTOM_RETRIABLE_EXCEPTION`
= `Field.createInternal("custom.retriable.exception")`; `errors.retriable.exception.pattern` does not exist and is
ignored). Expected: the task reconnects by itself and every row arrives at least once (duplicates reported).
This shows whether the retriable pattern is a usable mitigation for the non-retriable reconnect.
