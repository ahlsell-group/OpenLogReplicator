Regression test for fork fix/host-timezone-iana: "host-timezone": "Europe/Berlin", the zone of the
DB host, instead of a fixed offset. Commit timestamps must equal the real commit time in UTC
(commit_time check) all year; the +02:00 variant (commit-timestamp-host-tz) is right only in summer.
Upstream 2.0.0 rejects the name at startup with ERROR 30001 (known_issue; fixed_in lists the fork images).
