"""Kafka side of the Connect mode: read Debezium's topics and decode Avro (schema-registry wire format).

Values are converted to what olrt.normalize.canon expects, using the Connect logical type
of each column (the Avro field's "connect.name"):

  io.debezium.time.Timestamp / MicroTimestamp / NanoTimestamp   -> epoch ns (int)
  io.debezium.data.VariableScaleDecimal {scale, value}          -> decimal string
  Avro decimal (Connect Decimal)                                -> decimal string
  bytes                                                         -> upper-case hex
"""
import decimal
import io
import json
import time
import urllib.request

import fastavro
from confluent_kafka import Consumer, KafkaError
from confluent_kafka.admin import AdminClient

from . import settings as S

NS = {"io.debezium.time.Timestamp": 10**6, "io.debezium.time.MicroTimestamp": 10**3,
      "io.debezium.time.NanoTimestamp": 1}


class Registry:
    def __init__(self, url):
        self.url = url
        self.cache = {}

    def get(self, sid):
        if sid not in self.cache:
            with urllib.request.urlopen(f"{self.url}/schemas/ids/{sid}", timeout=10) as r:
                raw = json.loads(json.loads(r.read())["schema"])
            self.cache[sid] = (fastavro.parse_schema(raw), raw, _logical_names(raw))
        return self.cache[sid]


def _type_obj(t):
    """The non-null branch of a union, else the type itself."""
    if isinstance(t, list):
        t = [x for x in t if x != "null"]
        return t[0] if t else "null"
    return t


def _logical_names(raw):
    """column -> Connect logical name for the before/after row schema of an envelope."""
    out = {}
    if not isinstance(raw, dict) or raw.get("type") != "record":
        return out
    for f in raw.get("fields", []):
        if f["name"] not in ("before", "after"):
            continue
        rec = _type_obj(f["type"])
        if isinstance(rec, dict) and rec.get("type") == "record":
            for c in rec.get("fields", []):
                t = _type_obj(c["type"])
                if isinstance(t, dict):
                    out[c["name"]] = t.get("connect.name") or t.get("logicalType") or t.get("type")
                else:
                    out[c["name"]] = t
    return out


def convert_value(v, logical):
    if v is None:
        return None
    if logical in NS and isinstance(v, int):
        return v * NS[logical]
    if logical == "io.debezium.data.VariableScaleDecimal" and isinstance(v, dict):
        unscaled = int.from_bytes(v["value"], "big", signed=True)
        return str(decimal.Decimal(unscaled).scaleb(-int(v["scale"])))
    if isinstance(v, decimal.Decimal):
        return str(v)
    if isinstance(v, (bytes, bytearray)):
        return bytes(v).hex().upper()
    return v


def convert_row(row, names):
    if row is None:
        return None
    return {k: convert_value(v, names.get(k)) for k, v in row.items()}


def decode(reg, data):
    if data is None:
        return None, None
    if len(data) < 5 or data[0] != 0:
        return {"undecodable": data[:200].hex()}, None
    sid = int.from_bytes(data[1:5], "big")
    parsed, raw, names = reg.get(sid)
    return fastavro.schemaless_reader(io.BytesIO(data[5:]), parsed), (sid, names)


class TopicReader:
    """Reads every topic of one connector (prefix.*) from the beginning, incrementally."""

    def __init__(self, prefix):
        self.prefix = prefix
        self.reg = Registry(f"http://127.0.0.1:{S.SR_PORT}")
        self.c = Consumer({
            "bootstrap.servers": f"127.0.0.1:{S.KAFKA_PORT}",
            "group.id": f"olrt-reader-{prefix}-{time.time_ns()}",
            "auto.offset.reset": "earliest",
            "enable.auto.commit": False,
            "topic.metadata.refresh.interval.ms": 1000,
        })
        self.c.subscribe([f"^{prefix.replace('.', '[.]')}[.].*"])
        self.records = []
        # a regex subscription rebalances when a new topic appears; without committed offsets
        # the consumer then re-reads partitions from the start, so drop (topic, partition, offset) repeats
        self.seen = set()

    def poll(self, timeout=1.0):
        """Returns the number of new records."""
        n = 0
        msgs = self.c.consume(500, timeout)
        for m in msgs:
            if m.error():
                if m.error().code() not in (KafkaError._PARTITION_EOF, KafkaError.UNKNOWN_TOPIC_OR_PART):
                    self.records.append({"topic": m.topic(), "error": str(m.error())})
                continue
            pos = (m.topic(), m.partition(), m.offset())
            if pos in self.seen:
                continue
            self.seen.add(pos)
            key, _ = decode(self.reg, m.key()) if m.key() else (None, None)
            val, meta = decode(self.reg, m.value())
            rec = {"topic": m.topic(), "partition": m.partition(), "offset": m.offset(),
                   "ts": m.timestamp()[1], "key": _jsonable(key), "value_schema_id": meta[0] if meta else None}
            if val is not None and meta and "op" in val:
                names = meta[1]
                rec["value"] = {**{k: _jsonable(v) for k, v in val.items() if k not in ("before", "after")},
                                "before": _jsonable(convert_row(val.get("before"), names)),
                                "after": _jsonable(convert_row(val.get("after"), names))}
            else:
                rec["value"] = _jsonable(val)
            self.records.append(rec)
            n += 1
        return n

    def drain_until_idle(self, idle=5.0, timeout=120.0, min_records=0):
        """Poll until nothing new arrived for `idle` seconds (and at least min_records new ones)."""
        t0 = time.time()
        last = time.time()
        got = 0
        while time.time() - t0 < timeout:
            n = self.poll(1.0)
            got += n
            if n:
                last = time.time()
            elif time.time() - last >= idle and got >= min_records:
                return got, False
        return got, True

    def close(self):
        self.c.close()


def _jsonable(v):
    if isinstance(v, dict):
        return {k: _jsonable(x) for k, x in v.items()}
    if isinstance(v, list):
        return [_jsonable(x) for x in v]
    if isinstance(v, decimal.Decimal):
        return str(v)
    if isinstance(v, (bytes, bytearray)):
        return bytes(v).hex().upper()
    return v


def dump_topic(topic, timeout=15.0):
    """All values of a (small, plain-JSON) topic, e.g. Debezium's schema history."""
    c = Consumer({"bootstrap.servers": f"127.0.0.1:{S.KAFKA_PORT}",
                  "group.id": f"olrt-dump-{time.time_ns()}", "auto.offset.reset": "earliest",
                  "enable.auto.commit": False, "enable.partition.eof": True})
    c.subscribe([topic])
    out, t0 = [], time.time()
    try:
        while time.time() - t0 < timeout:
            m = c.poll(1.0)
            if m is None:
                continue
            if m.error():
                if m.error().code() == KafkaError._PARTITION_EOF:
                    break
                continue
            out.append(m.value().decode("utf-8", "replace") if m.value() else None)
    finally:
        c.close()
    return out


def dump_offsets(topic, connector, timeout=20.0):
    """Connect offset-storage records of one connector (all partitions), oldest first:
    {"ts", "partition", "offset", "key", "value"}."""
    from confluent_kafka import TopicPartition
    c = Consumer({"bootstrap.servers": f"127.0.0.1:{S.KAFKA_PORT}",
                  "group.id": f"olrt-dump-{time.time_ns()}", "auto.offset.reset": "earliest",
                  "enable.auto.commit": False, "enable.partition.eof": True})
    md = c.list_topics(topic, timeout=10).topics[topic]
    parts = sorted(md.partitions)
    c.assign([TopicPartition(topic, p, 0) for p in parts])
    out, eof, t0 = [], set(), time.time()
    try:
        while len(eof) < len(parts) and time.time() - t0 < timeout:
            m = c.poll(1.0)
            if m is None:
                continue
            if m.error():
                if m.error().code() == KafkaError._PARTITION_EOF:
                    eof.add(m.partition())
                continue
            key = m.key().decode("utf-8", "replace") if m.key() else None
            if not key or f'"{connector}"' not in key:
                continue
            val = m.value().decode("utf-8", "replace") if m.value() else None
            out.append({"ts": m.timestamp()[1], "partition": m.partition(), "offset": m.offset(),
                        "key": key, "value": json.loads(val) if val else None})
    finally:
        c.close()
    return sorted(out, key=lambda o: (o["ts"], o["offset"]))


def delete_topics(prefix):
    """Delete one connector run's topics on the lab broker (Redpanda --memory=1G allows 256
    partitions). The records are already in kafka.jsonl."""
    a = AdminClient({"bootstrap.servers": f"127.0.0.1:{S.KAFKA_PORT}"})
    names = [t for t in a.list_topics(timeout=10).topics
             if t.startswith(prefix + ".") or t in (f"olrt-schema-history.{prefix}", f"__debezium-heartbeat.{prefix}")]
    for f in a.delete_topics(names, operation_timeout=30).values() if names else []:
        f.result()
    return names
