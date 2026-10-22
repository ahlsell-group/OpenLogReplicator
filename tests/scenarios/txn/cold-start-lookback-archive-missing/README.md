The fork's cold start reads from the oldest open transaction's log when V$ARCHIVED_LOG says it is available. Here the
file is removed from disk after the transaction began, so V$ARCHIVED_LOG still lists it. Shows what OLR does: a
loud stop, a crash loop on 10009, or a fallback to the log holding the start SCN with a warning naming the
transaction it gives up. The transaction committed after START must not be silently lost.
