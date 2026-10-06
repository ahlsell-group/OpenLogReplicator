DECLARE
  TYPE nt IS TABLE OF NUMBER;
  TYPE vt IS TABLE OF VARCHAR2(40);
  ids nt := nt(); q nt := nt(); v vt := vt();
BEGIN
  FOR i IN 1 .. 300 LOOP ids.EXTEND; q.EXTEND; v.EXTEND; ids(i) := 1000 + i; q(i) := i * 1.25; v(i) := 'ins' || i; END LOOP;
  FORALL i IN 1 .. ids.COUNT INSERT INTO olrt_fa.t (id, qty, txt) VALUES (ids(i), q(i), v(i));
  ids.DELETE; q.DELETE; v.DELETE;
  FOR i IN 1 .. 200 LOOP ids.EXTEND; q.EXTEND; v.EXTEND; ids(i) := i * 2; q(i) := -i; v(i) := CASE WHEN MOD(i, 7) = 0 THEN NULL ELSE 'upd' || i END; END LOOP;
  FORALL i IN 1 .. ids.COUNT UPDATE olrt_fa.t SET qty = q(i), txt = v(i) WHERE id = ids(i);
  -- sparse collection
  ids.DELETE(5, 50);
  FORALL i IN INDICES OF ids DELETE FROM olrt_fa.t WHERE id = ids(i) + 1;
END;
/
COMMIT;
DECLARE
  TYPE nt IS TABLE OF NUMBER;
  ids nt := nt(1, 2, 3, 3, 4, 1001, 1001);
  errs NUMBER;
  bulk_errors EXCEPTION;
  PRAGMA EXCEPTION_INIT(bulk_errors, -24381);
BEGIN
  -- duplicate keys fail individually, the rest survive
  FORALL i IN 1 .. ids.COUNT SAVE EXCEPTIONS INSERT INTO olrt_fa.t (id, qty, txt) VALUES (ids(i), 0, 'se');
EXCEPTION WHEN bulk_errors THEN errs := SQL%BULK_EXCEPTIONS.COUNT;
END;
/
COMMIT;
