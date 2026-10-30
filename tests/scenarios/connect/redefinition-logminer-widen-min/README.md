`redefinition-widen-min` on Debezium's LogMiner adapter (hybrid): after FINISH widens QTY to NUMBER(18,4) and
adds EXTRA, the first UPDATE of T stops the LogMiner session with "Oracle LogMiner is unable to re-construct
the SQL for 'UPDATE' event" and the task fails; no row written after the swap is delivered.
