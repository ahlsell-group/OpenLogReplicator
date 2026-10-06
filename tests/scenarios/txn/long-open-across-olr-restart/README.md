The workaround path for #330: OLR restarts with <db>-chkpt.json deleted first (as an init
container might do), so Debezium sends START with its stored offset. Begin-SCN stamping predicts no loss here but a replay
from the begin SCN of the last delivered transaction (duplicates are allowed and deduped
by the replay check).
