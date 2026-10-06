The configuration-only mitigation for meta/commit-timestamp on 2.0.0: "host-timezone": "+02:00" (the DB
host's summer-time UTC offset, CEST). Passes in summer time only; a fixed offset is wrong for
half the year (the fork's fix/host-timezone-iana accepts an IANA name instead). +02:00 matches
Europe/Berlin from the last Sunday of March to the last Sunday of October.
