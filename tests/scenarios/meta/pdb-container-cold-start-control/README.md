# meta/pdb-container-cold-start-control

The same cold START, tables and workload as `meta/root-container-cold-start`, but in FREEPDB1.
There `SYS.V_$PDBS` has a row for the container, so the pdb id OLR reads is defined and every
build delivers the three transactions. Together with the root scenario it isolates the NULL pdb
id as the only difference.
