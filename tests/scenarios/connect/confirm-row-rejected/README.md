The table topic is created with max.message.bytes 3000. T writes a small row, a row of about 8 000 random characters
and another small row. The broker rejects the large row (RecordTooLargeException, not retriable) and the task fails,
while the heartbeat and transaction END records of T's commit, on other topics, are acknowledged. Then the topic
limit is raised and the task restarted.

CONFIRM must cover only offsets Kafka Connect flushed (all earlier records written). A build that confirms the offset
of the last acknowledged record can confirm T's commit before the failure; OLR then releases T, the restarted task
continues from the flushed offset before T, OLR logs WARN 60039 and T is lost. Whether that happens depends on the
order of acknowledgements, so a pass on such a build proves nothing; a failure on a fixed build does.
