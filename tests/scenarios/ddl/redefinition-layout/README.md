As `redefinition-mid-stream`, but the interim table has another column layout: TXT moves before QTY and
both get wider (VARCHAR2(80), NUMBER(18,4)), D moves before CODE, CODE widens to NUMBER(12) and EXTRA
(nullable, no default) is new. After FINISH the same column names have other column numbers, so a tool that
kept the old column map would decode the new redo into the wrong columns. The copy changes no values, so
replay applies; `redefinition-type-change` covers a type change through the column mapping.
