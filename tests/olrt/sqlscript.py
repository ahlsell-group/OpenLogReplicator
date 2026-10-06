"""Minimal SQL script splitter for scenario files.

Statements end with ';' at end of line. PL/SQL blocks (BEGIN/DECLARE/CREATE ... PROCEDURE
etc.) end with a line holding only '/'. SQL*Plus commands (SET, PROMPT, WHENEVER, ...) are
ignored. Directives are comment lines starting with '-- @':

  -- @session N          run following statements in session N (default 1)
  -- @switch_logfile     ALTER SYSTEM ARCHIVE LOG CURRENT (from CDB$ROOT)
  -- @sleep SECONDS
  -- @expect_error ORA-NNNNN   the next statement must fail with this error
"""
import dataclasses
import re

SQLPLUS = re.compile(r"^\s*(SET|PROMPT|WHENEVER|SPOOL|EXIT|COLUMN|SHOW|REM)\b", re.I)
PLSQL_START = re.compile(
    r"^\s*(BEGIN|DECLARE|CREATE\s+(OR\s+REPLACE\s+)?(PROCEDURE|FUNCTION|PACKAGE|TRIGGER|TYPE))\b", re.I)
DIRECTIVE = re.compile(r"^\s*--\s*@(\w+)\s*(.*?)\s*$")


@dataclasses.dataclass
class Step:
    kind: str            # "sql" | "session" | "switch_logfile" | "sleep" | "expect_error"
    arg: str = ""
    line: int = 0


def parse(text: str) -> list[Step]:
    steps: list[Step] = []
    buf: list[str] = []
    plsql = False
    start = 0
    for no, raw in enumerate(text.splitlines(), start=1):
        line = raw.rstrip()
        if not buf:
            m = DIRECTIVE.match(line)
            if m:
                steps.append(Step(m.group(1).lower(), m.group(2), no))
                continue
            stripped = line.strip()
            if not stripped or stripped.startswith("--") or SQLPLUS.match(line):
                continue
            plsql = bool(PLSQL_START.match(line))
            start = no
        if plsql:
            if line.strip() == "/":
                steps.append(Step("sql", "\n".join(buf).strip(), start))
                buf = []
            else:
                buf.append(line)
            continue
        buf.append(line)
        if line.endswith(";"):
            stmt = "\n".join(buf).strip()[:-1].strip()
            steps.append(Step("sql", stmt, start))
            buf = []
    if buf and "\n".join(buf).strip():
        raise ValueError(f"unterminated statement starting at line {start}")
    return steps
