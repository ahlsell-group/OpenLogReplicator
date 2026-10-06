"""Lab-wide constants. Everything Docker-side is prefixed olrt- so it can be told apart
from other containers on the same engine."""
import os
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCENARIOS = ROOT / "scenarios"
FIXTURES = ROOT / "fixtures"
RESULTS = pathlib.Path(os.environ.get("OLRT_RESULTS", ROOT / "results"))

ORACLE_IMAGE = os.environ.get(
    "OLRT_ORACLE_IMAGE",
    "gvenzl/oracle-free:23.26.2-slim@sha256:6c6b2555e19b81beb79f3429ac34f241b196785c0137125b871f0af8507685b1",
)
# OLRT_INSTANCE lets several suites run side by side on one engine (own DB, network, volume, containers)
INSTANCE = os.environ.get("OLRT_INSTANCE", "")
_SUFFIX = f"-{INSTANCE}" if INSTANCE else ""
ORACLE_CONTAINER = f"olrt-oracle{_SUFFIX}"
NETWORK = f"olrt-net{_SUFFIX}"
VOLUME = f"olrt-oradata{_SUFFIX}"
ORACLE_PORT = int(os.environ.get("OLRT_ORACLE_PORT", "15210"))
ORACLE_PASSWORD = "oracle"
# DB host time zone with DST; redo timestamps are host-local wall time.
ORACLE_TZ = os.environ.get("OLRT_ORACLE_TZ", "Europe/Berlin")
PDB = "FREEPDB1"
# gid of oinstall in the Oracle image: redo files are 0640 oracle:oinstall
ORACLE_GID = 54321

OLR_TIMEOUT_S = int(os.environ.get("OLRT_OLR_TIMEOUT", "300"))
OLR_PARALLEL = int(os.environ.get("OLRT_OLR_PARALLEL", "4"))

# Debezium Connect mode (olrt.connect): broker with schema registry + one Kafka Connect worker
BROKER_IMAGE = os.environ.get("OLRT_BROKER_IMAGE", "redpandadata/redpanda:v24.2.7")
BROKER_CONTAINER = f"olrt-redpanda{_SUFFIX}"
CONNECT_CONTAINER = f"olrt-connect{_SUFFIX}"
CONNECT_IMAGE = os.environ.get("OLRT_CONNECT_IMAGE", "olrt/connect:{version}")
DEBEZIUM_VERSIONS = ["3.6.1.Final", "3.7.0.Final"]
# host ports derive from the Oracle port so instances do not collide (15213 -> 25213/26213/27213)
KAFKA_PORT = int(os.environ.get("OLRT_KAFKA_PORT", str(ORACLE_PORT + 10000)))
SR_PORT = int(os.environ.get("OLRT_SR_PORT", str(ORACLE_PORT + 11000)))
CONNECT_PORT = int(os.environ.get("OLRT_CONNECT_PORT", str(ORACLE_PORT + 12000)))
# Debezium's database user (common user, as for a LogMiner/OLR connector account)
DBZ_USER = "c##dbzuser"
DBZ_PASSWORD = "dbz"
