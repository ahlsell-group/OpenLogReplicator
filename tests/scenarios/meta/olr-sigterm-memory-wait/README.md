SIGTERM while OLR's parser waits for builder memory. The client stops confirming after the first transaction, a
50,000-row transaction fills the 4 MB builder buffer (memory is released only on confirm), the parser blocks in
getMemoryChunk, and the run ends with SIGTERM like a pod stop. Expected: "caught signal: 15", exit status 0, no crash.
