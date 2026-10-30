As `redefinition-mid-stream`, but the interim table has another layout and types: TXT moves before QTY and
both get wider, CODE changes from NUMBER to VARCHAR2 through the column mapping (`TO_CHAR(code) code`), D is
dropped and EXTRA (DEFAULT 'dflt') is new. After FINISH the same names have other column numbers and types,
so a tool that kept the old column map would decode the new redo wrongly. The diff against LogMiner checks
every row image before and after the swap. Replay cannot pass by construction: the copy changes every row's
CODE and EXTRA without a change event on T (see known_issue); `redefinition-layout` is the replayable variant.
