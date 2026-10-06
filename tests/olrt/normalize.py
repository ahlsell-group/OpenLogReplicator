"""Canonical column values, so the database snapshot, LogMiner and OLR can be compared.

Oracle NUMBER stores no scale: NUMBER(15,3) holding 0.000 is the same bytes as 0. The
canonical form is therefore the exact decimal value (never a float), printed without
exponent and without trailing zeros. A value with more fractional digits than the column
scale, or any float rounding on the way, shows up as a difference. Consumers that need the
declared scale must take it from the schema; see README.md.

Canonical forms (all strings, or None for NULL):
  NUMBER/FLOAT       "-12.5", "0", "123456789012.345"
  DATE               "2026-03-29T02:30:00"
  TIMESTAMP(n)       "2026-03-29T02:30:00.123456789" (always 9 fraction digits)
  CHAR/VARCHAR2/...  the string as stored (CHAR keeps its blank padding)
  RAW                upper-case hex
  BINARY_FLOAT/DOUBLE  repr() of the IEEE value (single precision for BINARY_FLOAT)
"""
import datetime as dt
import decimal
import re
import struct

EPOCH = dt.datetime(1970, 1, 1)
ISO = re.compile(r"^(\d{4}-\d{2}-\d{2})[T ](\d{2}:\d{2}:\d{2})(?:\.(\d{1,9}))?Z?$")


def kind(data_type: str) -> str:
    t = data_type.upper()
    if t in ("NUMBER", "FLOAT", "INTEGER"):
        return "num"
    if t == "DATE":
        return "date"
    if t.startswith("TIMESTAMP") and "TIME ZONE" not in t:
        return "ts"
    if t in ("RAW",):
        return "raw"
    if t in ("BINARY_FLOAT", "BINARY_DOUBLE"):
        return "bin"
    return "str"


def select_expr(col: str, data_type: str) -> str:
    """SQL expression that fetches a column losslessly for a snapshot."""
    k = kind(data_type)
    q = f'"{col}"'
    if k == "date":
        return f"TO_CHAR({q}, 'YYYY-MM-DD\"T\"HH24:MI:SS')"
    if k == "ts":
        return f"TO_CHAR({q}, 'YYYY-MM-DD\"T\"HH24:MI:SS.FF9')"
    if k == "raw":
        return f"RAWTOHEX({q})"
    return q


def _num(v):
    if isinstance(v, bool):
        raise TypeError("bool for NUMBER")
    if isinstance(v, float):
        # never expected: JSON is parsed with Decimal; keep repr so a float shows up in diffs
        return "float:" + repr(v)
    d = decimal.Decimal(str(v).strip())
    if d.is_zero():
        return "0"
    s = format(d.normalize(), "f")
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return s


def _from_epoch_ns(ns: int, with_fraction: bool):
    secs, nanos = divmod(int(ns), 10**9)
    t = EPOCH + dt.timedelta(seconds=secs)
    base = t.strftime("%Y-%m-%dT%H:%M:%S")
    return f"{base}.{nanos:09d}" if with_fraction else (base if nanos == 0 else f"{base}.{nanos:09d}")


def _datetime(v, k):
    with_fraction = k == "ts"
    if isinstance(v, (int, decimal.Decimal)) and not isinstance(v, bool):
        return _from_epoch_ns(int(v), with_fraction)
    if isinstance(v, dt.datetime):
        base = v.strftime("%Y-%m-%dT%H:%M:%S")
        return f"{base}.{v.microsecond * 1000:09d}" if with_fraction else base
    s = str(v).strip()
    if s.isdigit() or (s.startswith("-") and s[1:].isdigit()):
        return _from_epoch_ns(int(s), with_fraction)
    m = ISO.match(s)
    if not m:
        return "unparsed:" + s
    frac = (m.group(3) or "").ljust(9, "0")
    base = f"{m.group(1)}T{m.group(2)}"
    if with_fraction:
        return f"{base}.{frac}"
    return base if int(frac or 0) == 0 else f"{base}.{frac}"


def canon(v, data_type: str):
    if v is None:
        return None
    k = kind(data_type)
    try:
        if k == "num":
            return _num(v)
        if k in ("date", "ts"):
            return _datetime(v, k)
        if k == "raw":
            return str(v).upper()
        if k == "bin":
            # IEEE value, shortest repr; BINARY_FLOAT rounded to single precision first, so
            # "0.10000000000000001" (LogMiner) and 0.1 (OLR) are the same double
            f = float(str(v)) if not isinstance(v, float) else v
            if data_type.upper() == "BINARY_FLOAT":
                f = struct.unpack("f", struct.pack("f", f))[0]
            return repr(f)
        return v if isinstance(v, str) else str(v)
    except (decimal.InvalidOperation, ValueError, TypeError) as e:
        return f"unparsed:{v!r} ({e})"
