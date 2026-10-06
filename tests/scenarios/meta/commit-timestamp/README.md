OLR emits redo timestamps (host-local wall time) as if they were UTC, so events are
off by the DB host's UTC offset, and DST is not applied. The lab DB host runs on
Europe/Berlin. Expected to fail on all 2.0.0 builds.
