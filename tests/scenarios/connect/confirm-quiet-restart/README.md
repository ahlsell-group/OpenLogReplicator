One captured transaction, then 1 500 single-LWN commits outside the filter with queue-size 200, then the
Connect worker is SIGKILLed and one more captured row is written. On OLR builds that queue unsent checkpoints
(2.0.0-ahlsell.8) Debezium 3.7.0 leaves its last transaction unconfirmed and the writer stops (the last row is
lost for the run); with the completed position confirmed the queue drains. After the restart the task
continues from the confirmed position; run.json shows olr_c_scn/olr_c_idx past the transaction.
