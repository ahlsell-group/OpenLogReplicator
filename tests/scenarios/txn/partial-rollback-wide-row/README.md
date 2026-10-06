ROLLBACK TO SAVEPOINT touching a >255-column row (several row pieces,
0x0B10 supplemental pieces) after SELECT FOR UPDATE. OLR may log WARN 70003 and emit the
rolled-back change at commit as a phantom. Rows 252/1252 are not used by other wide scenarios.
