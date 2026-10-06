"""Reference events from LogMiner over the same archived redo and SCN range.

DICT_FROM_ONLINE_CATALOG + COMMITTED_DATA_ONLY, run as SYS in CDB$ROOT and filtered on
SRC_CON_NAME. Column values come from DBMS_LOGMNR.MINE_VALUE on REDO_VALUE (after) and
UNDO_VALUE (before), formatted by the session NLS settings in db.NLS.
"""
from . import normalize
from . import settings as S

ID_COLS = "rbasqn, rbablk, rbabyte, scn, commit_scn, operation, seg_owner, table_name, " \
          "xidusn, xidslt, xidsqn, rawtohex(xid), csf, " \
          "TO_CHAR(timestamp, 'YYYY-MM-DD\"T\"HH24:MI:SS'), TO_CHAR(commit_timestamp, 'YYYY-MM-DD\"T\"HH24:MI:SS'), " \
          "sql_redo, info, rollback, row_id, status"
MAX_EXPR = 900


def _ident(r):
    return (int(r[0]), int(r[1]), int(r[2]), int(r[3]))


def mine(rconn, archived, start_scn, end_scn, tables, dictionary="online", con_name=S.PDB):
    cur = rconn.cursor()
    for i, a in enumerate(archived):
        cur.execute("BEGIN DBMS_LOGMNR.ADD_LOGFILE(:n, :o); END;", n=a[1], o=1 if i == 0 else 3)
    opts = ("DBMS_LOGMNR.DICT_FROM_REDO_LOGS + DBMS_LOGMNR.DDL_DICT_TRACKING" if dictionary == "redo"
            else "DBMS_LOGMNR.DICT_FROM_ONLINE_CATALOG")
    cur.execute(f"""BEGIN DBMS_LOGMNR.START_LOGMNR(STARTSCN => :a, ENDSCN => :b,
                   OPTIONS => {opts} + DBMS_LOGMNR.COMMITTED_DATA_ONLY); END;""",
                a=start_scn, b=end_scn)
    try:
        owners = sorted({t.split(".")[0] for t in tables})
        names = sorted({t.split(".")[1] for t in tables})
        where = (f"src_con_name = '{con_name}' AND ((seg_owner IN ({','.join(repr(o) for o in owners)}) "
                 f"AND table_name IN ({','.join(repr(n) for n in names)})) OR operation IN ('COMMIT', 'ROLLBACK'))")
        base = cur.execute(f"SELECT {ID_COLS} FROM v$logmnr_contents WHERE {where}").fetchall()

        # values per table, chunked so no SELECT exceeds Oracle's 1000-expression limit
        values = {}  # (ident, occurrence) -> (before, after)
        for t, info in tables.items():
            if info is None:
                continue
            owner, table = t.split(".")
            cols = info["columns"]
            per = MAX_EXPR // 3
            for c0 in range(0, len(cols), per):
                chunk = cols[c0:c0 + per]
                exprs = []
                for c in chunk:
                    path = f"{owner}.{table}.{c['name']}"
                    exprs.append(f"DBMS_LOGMNR.MINE_VALUE(undo_value, '{path}')")
                    exprs.append(f"DBMS_LOGMNR.MINE_VALUE(redo_value, '{path}')")
                    exprs.append(f"DBMS_LOGMNR.COLUMN_PRESENT(redo_value, '{path}')")
                rows = cur.execute(
                    f"SELECT rbasqn, rbablk, rbabyte, scn, csf, {', '.join(exprs)} FROM v$logmnr_contents "
                    f"WHERE src_con_name = '{con_name}' AND seg_owner = :o AND table_name = :t",
                    o=owner, t=table).fetchall()
                seen = {}
                for r in rows:
                    # a long SQL_REDO is split over CSF=1 continuation rows, but only when
                    # SQL_REDO is selected; number events by their final (CSF=0) row on both sides
                    if int(r[4] or 0) == 1:
                        continue
                    ident = _ident(r)
                    occ = seen.get(ident, 0)
                    seen[ident] = occ + 1
                    b, a = values.setdefault((ident, occ), ({}, {}))
                    for j, c in enumerate(chunk):
                        b[c["name"]] = normalize.canon(r[5 + 3 * j], c["type"])
                        if r[7 + 3 * j]:
                            a[c["name"]] = normalize.canon(r[6 + 3 * j], c["type"])
    finally:
        cur.execute("BEGIN DBMS_LOGMNR.END_LOGMNR; END;")

    txns = {}
    order = []
    seen = {}
    for r in base:
        (rbasqn, rbablk, rbabyte, scn, commit_scn, op, owner, table, usn, slt, sqn, xidraw, csf,
         ts, cts, sql_redo, info, rollback, row_id, status) = r
        xid = (int(usn), int(slt), int(sqn))
        tx = txns.get(xid)
        if tx is None:
            tx = txns[xid] = {"xid": list(xid), "xid_raw": xidraw, "commit_scn": None,
                              "commit_time": None, "events": [], "unsupported": []}
            order.append(xid)
        if op == "COMMIT":
            tx["commit_scn"] = int(commit_scn if commit_scn is not None else scn)
            tx["commit_time"] = cts or ts
            continue
        if op == "ROLLBACK":
            tx["rolled_back"] = True
            continue
        t = f"{owner}.{table}"
        if int(csf or 0) == 1:
            continue  # continuation row of a long SQL_REDO, see above
        ident = _ident(r)
        occ = seen.get(ident, 0)
        seen[ident] = occ + 1
        if op not in ("INSERT", "UPDATE", "DELETE"):
            tx["unsupported"].append({"op": op, "table": t, "info": info, "sql": (sql_redo or "")[:300]})
            continue
        b, a = values.get((ident, occ), ({}, {}))
        ev = {"op": {"INSERT": "c", "UPDATE": "u", "DELETE": "d"}[op], "table": t, "scn": int(scn),
              "rowid": row_id}
        if int(status or 0) != 0 or (info and "Dictionary" in info):
            # LogMiner could not decode the row with its dictionary (typically redo written
            # before a DDL, mined with the online catalog). The diff check skips its values.
            ev["undecodable"] = f"status={status} {info or ''}".strip()
        if op in ("UPDATE", "DELETE"):
            ev["before"] = b
        if op in ("INSERT", "UPDATE"):
            ev["after"] = a
        if int(rollback or 0):
            # ROLLBACK TO SAVEPOINT shows up as compensating rows with ROLLBACK=1. The net
            # effect a CDC consumer should see is "neither": drop the latest earlier event
            # on the same row that this one compensates.
            undo = {"c": "d", "d": "c", "u": "u"}[ev["op"]]
            for k in range(len(tx["events"]) - 1, -1, -1):
                e = tx["events"][k]
                if e["rowid"] == row_id and e["op"] == undo and e["table"] == t:
                    del tx["events"][k]
                    break
            else:
                tx["unsupported"].append({"op": "ROLLBACK-ROW", "table": t, "info": "no event to compensate",
                                          "sql": (sql_redo or "")[:300]})
            tx["partial_rollback"] = True
            continue
        tx["events"].append(ev)
    # A transaction that ended in ROLLBACK without a COMMIT (e.g. a prepared XA branch that was
    # rolled back) is not part of the net effect even when LogMiner still lists its rows.
    out = [txns[x] for x in order if (txns[x]["events"] or txns[x]["unsupported"])
           and not (txns[x].get("rolled_back") and txns[x]["commit_scn"] is None)]
    out.sort(key=lambda t: (t["commit_scn"] or 0))
    return {"transactions": out}
