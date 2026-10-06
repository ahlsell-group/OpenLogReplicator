Direct-path load into a range-partitioned table: the 100 APPEND rows land in both partitions (ids 902-1100),
so the OP 19.1 data blocks carry two different partition data object ids. OLR cannot decode them; with the
direct-path warning it must still name the table (60042) by mapping the partition's data object id back to
OLRT_DPP.T. The conventional row (id 1) and its update are the control.
