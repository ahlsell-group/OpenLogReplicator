CHAR values are stored blank-padded and must arrive padded ('ab' in CHAR(10) is 'ab' plus
8 blanks). Covers CHAR(1) J/N flags, an all-blank CHAR, NCHAR, and an
update that only changes padding-relevant length.
