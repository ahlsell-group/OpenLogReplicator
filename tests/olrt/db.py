"""Local Oracle Free container and connections. Only ever talks to the olrt-oracle container."""
import subprocess
import time

import oracledb

from . import settings as S

oracledb.defaults.fetch_decimals = True

NLS = [
    "ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD\"T\"HH24:MI:SS'",
    "ALTER SESSION SET NLS_TIMESTAMP_FORMAT = 'YYYY-MM-DD\"T\"HH24:MI:SS.FF9'",
    "ALTER SESSION SET NLS_NUMERIC_CHARACTERS = '.,'",
]


def docker(*args, check=True, capture=True):
    return subprocess.run(["docker", *args], check=check, text=True,
                          capture_output=capture)


def _exists(kind, name):
    return docker(kind, "inspect", name, check=False).returncode == 0


def up(wait=True):
    """Create (or start) the olrt-oracle container. Idempotent."""
    if not _exists("network", S.NETWORK):
        docker("network", "create", S.NETWORK)
    if not _exists("volume", S.VOLUME):
        docker("volume", "create", S.VOLUME)
    if not _exists("container", S.ORACLE_CONTAINER):
        docker("run", "-d", "--name", S.ORACLE_CONTAINER, "--network", S.NETWORK,
               "-p", f"127.0.0.1:{S.ORACLE_PORT}:1521", "--shm-size", "1g",
               "-e", f"ORACLE_PASSWORD={S.ORACLE_PASSWORD}", "-e", f"TZ={S.ORACLE_TZ}",
               "-v", f"{S.VOLUME}:/opt/oracle/oradata",
               "-v", f"{S.ROOT / 'docker' / 'oracle-init'}:/container-entrypoint-initdb.d:ro",
               "--label", "olrt=1", S.ORACLE_IMAGE)
    else:
        docker("start", S.ORACLE_CONTAINER)
    if wait:
        wait_ready()
    prepare()


def down(volume=False):
    docker("rm", "-f", S.ORACLE_CONTAINER, check=False)
    if volume:
        docker("volume", "rm", S.VOLUME, check=False)


def wait_ready(timeout=900):
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = docker("inspect", "-f", "{{.State.Status}}", S.ORACLE_CONTAINER, check=False).stdout.strip()
        if st.startswith("exited"):
            raise RuntimeError("olrt-oracle exited:\n" + docker("logs", "--tail", "40", S.ORACLE_CONTAINER,
                                                                 check=False).stderr)
        if st == "running":
            # the image has no healthcheck; the entrypoint prints this after the init scripts
            logs = docker("logs", S.ORACLE_CONTAINER, check=False)
            if "DATABASE IS READY TO USE" in logs.stdout + logs.stderr:
                try:
                    with root() as c:
                        c.cursor().execute("SELECT 1 FROM dual")
                    return
                except oracledb.Error:
                    pass
        time.sleep(5)
    raise TimeoutError("olrt-oracle not ready")


def _connect(service):
    return oracledb.connect(user="sys", password=S.ORACLE_PASSWORD,
                            dsn=f"127.0.0.1:{S.ORACLE_PORT}/{service}",
                            mode=oracledb.AUTH_MODE_SYSDBA)


def root():
    """SYS in CDB$ROOT (log switches, LogMiner, database-level settings)."""
    c = _connect("FREE")
    for s in NLS:
        c.cursor().execute(s)
    return c


def pdb():
    """SYS in FREEPDB1 (scenario SQL, snapshots)."""
    c = _connect(S.PDB)
    for s in NLS:
        c.cursor().execute(s)
    return c


def scenario_conn(sc):
    """SYS in the scenario's container (scenario SQL, snapshots)."""
    return root() if sc.container == "root" else pdb()


OLR_GRANTS_SYS = ['CCOL$', 'CDEF$', 'COL$', 'DEFERRED_STG$', 'ECOL$', 'LOB$', 'LOBCOMPPART$', 'LOBFRAG$',
                  'OBJ$', 'TAB$', 'TABCOMPART$', 'TABPART$', 'TABSUBPART$', 'TS$', 'USER$']
OLR_GRANTS_VIEWS = ['V_$ARCHIVED_LOG', 'V_$DATABASE', 'V_$DATABASE_INCARNATION', 'V_$LOG', 'V_$LOGFILE', 'V_$PDBS',
                    'V_$PARAMETER', 'V_$STANDBY_LOG', 'V_$TRANSPORTABLE_PLATFORM', 'V_$TRANSACTION']


def ensure_root_olr_user(conn_root):
    """Common user C##OLR in CDB$ROOT with the grants of docker/oracle-init/02-olr-user.sql, for
    scenarios with container = "root". Idempotent."""
    cur = conn_root.cursor()
    try:
        cur.execute('CREATE USER c##olr IDENTIFIED BY "olr" DEFAULT TABLESPACE users CONTAINER = ALL')
    except oracledb.DatabaseError as e:
        if "ORA-01920" not in str(e):
            raise
        return
    cur.execute("GRANT CREATE SESSION TO c##olr")
    for t in OLR_GRANTS_SYS:
        cur.execute(f'GRANT SELECT, FLASHBACK ON SYS."{t}" TO c##olr')
    for t in OLR_GRANTS_VIEWS:
        cur.execute(f'GRANT SELECT ON SYS."{t}" TO c##olr')
    cur.execute("GRANT SELECT, FLASHBACK ON XDB.XDB$TTSET TO c##olr")
    for (name,) in cur.execute("""SELECT table_name FROM dba_tables WHERE owner = 'XDB'
                                    AND (table_name LIKE 'X$NM%' OR table_name LIKE 'X$PT%' OR table_name LIKE 'X$QN%')""").fetchall():
        cur.execute(f'GRANT SELECT, FLASHBACK ON XDB."{name}" TO c##olr')


def prepare():
    """Idempotent settings applied on every start."""
    with root() as c:
        cur = c.cursor()
        log_mode, = cur.execute("SELECT log_mode FROM v$database").fetchone()
        if log_mode != "ARCHIVELOG":
            raise RuntimeError("database not in ARCHIVELOG mode; init scripts failed? (make db-reset)")
        # OLR reads the dictionary AS OF the start SCN and snapshots use AS OF too;
        # keep undo long enough that a recording can be replicated later in the same run.
        cur.execute("ALTER SYSTEM SET undo_retention = 86400")
        ensure_root_olr_user(c)
    with pdb() as c:
        # also in 02-olr-user.sql; repeated here for volumes created without it (idempotent)
        c.cursor().execute("GRANT SELECT ON SYS.V_$TRANSACTION TO olr")
        # the PDB's undo is 31 MB, NOGUARANTEE: replicating a recording ~45 min later fails with
        # ORA-01555 on OLR's dictionary read AS OF the start SCN. Guarantee it (autoextends).
        c.cursor().execute("ALTER TABLESPACE undotbs1 RETENTION GUARANTEE")


def scalar(conn, sql, **binds):
    row = conn.cursor().execute(sql, binds).fetchone()
    return row[0] if row else None


def current_scn(conn):
    return int(scalar(conn, "SELECT current_scn FROM v$database"))


def current_seq(conn):
    return int(scalar(conn, "SELECT sequence# FROM v$log WHERE status = 'CURRENT' AND thread# = 1"))


def switch_logfile(conn_root):
    """Switch and wait until the old log is archived (ARCHIVE LOG CURRENT is synchronous)."""
    conn_root.cursor().execute("ALTER SYSTEM ARCHIVE LOG CURRENT")


def archived_logs(conn_root, first_seq, last_seq):
    rows = conn_root.cursor().execute(
        """SELECT sequence#, name, first_change#, next_change# FROM v$archived_log
            WHERE thread# = 1 AND sequence# BETWEEN :a AND :b AND name IS NOT NULL
              AND deleted = 'NO' AND standby_dest = 'NO'
            ORDER BY sequence#""", a=first_seq, b=last_seq).fetchall()
    seen = {}
    for seq, name, f, n in rows:
        seen.setdefault(int(seq), (int(seq), name, int(f), int(n)))
    return [seen[k] for k in sorted(seen)]


def set_force_logging(conn_root, on: bool):
    cur = conn_root.cursor()
    fl, = cur.execute("SELECT force_logging FROM v$database").fetchone()
    if on and fl != "YES":
        cur.execute("ALTER DATABASE FORCE LOGGING")
    elif not on and fl == "YES":
        cur.execute("ALTER DATABASE NO FORCE LOGGING")
