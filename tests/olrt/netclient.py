"""Client for OLR's network writer, behaving like Debezium 3.6.1's OlrNetworkClient.

Wire format (src/stream/StreamNetwork.cpp, v2.0.0): every message is a little-endian
uint32 length followed by the body; length 0xFFFFFFFF means a uint64 length follows.
Client -> OLR: protobuf RedoRequest. OLR -> client: protobuf RedoResponse for control
replies (INFO/START/CONTINUE), then raw JSON data messages (format json/debezium) once
streaming.

Debezium behaviour mimicked (OlrNetworkClient 3.6.x):
- connect(scn, idx): INFO; if REPLICATE -> CONTINUE(c_scn=scn, c_idx=idx) (or CONTINUE(scn)
  without idx); if READY -> START(scn). Second reply must be REPLICATE.
- with idx None the client skips events whose "scn" < start scn (skipToStartScn) and
  confirms them while skipping.
- confirm(scn, idx) sends CONFIRM only if prevScn != 0 and prevScn < scn; prevScn is
  updated on every call (so the first call never sends anything).
"""
import json
import socket
import struct
import decimal

from .proto import OraProtoBuf_pb2 as pb

MAX_LENGTH = 0xFFFFFFFF


class ConnectionLost(Exception):
    pass


class Client:
    def __init__(self, database, timeout=30.0):
        self.database = database
        self.timeout = timeout
        self.sock = None
        self.prev_scn = 0
        self.streaming = False
        self.skip_to_start_scn = None
        self.sent = []      # log of requests sent, for evidence

    # ------------------------------------------------------------- framing
    def connect_socket(self, host, port, retries=60, delay=0.5):
        import time
        last = None
        for _ in range(retries):
            try:
                s = socket.create_connection((host, port), timeout=self.timeout)
                s.settimeout(self.timeout)
                self.sock = s
                return
            except OSError as e:
                last = e
                time.sleep(delay)
        raise ConnectionLost(f"cannot connect to {host}:{port}: {last}")

    def close(self):
        if self.sock:
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.sock.close()
        self.sock = None
        self.streaming = False

    def _recv_exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionLost("Connection lost")
            buf += chunk
        return bytes(buf)

    def _read_frame(self):
        (length,) = struct.unpack("<I", self._recv_exact(4))
        if length == MAX_LENGTH:
            (length,) = struct.unpack("<Q", self._recv_exact(8))
        return self._recv_exact(length)

    def _send(self, req):
        body = req.SerializeToString()
        self.sock.sendall(struct.pack("<I", len(body)) + body)
        self.sent.append({"code": pb.RequestCode.Name(req.code),
                          **({"scn": req.scn} if req.HasField("scn") else {}),
                          **({"c_scn": req.c_scn} if req.HasField("c_scn") else {}),
                          **({"c_idx": req.c_idx} if req.HasField("c_idx") else {})})

    def _req(self, code):
        r = pb.RedoRequest()
        r.code = code
        r.database_name = self.database
        return r

    def _response(self):
        resp = pb.RedoResponse()
        resp.ParseFromString(self._read_frame())
        return resp

    # ------------------------------------------------------------- raw requests
    def info(self):
        self._send(self._req(pb.RequestCode.INFO))
        return self._response()

    def start(self, scn=None, seq=None):
        r = self._req(pb.RequestCode.START)
        r.scn = scn if scn is not None else 0xFFFFFFFFFFFFFFFF
        if seq is not None:
            r.seq = seq
        self._send(r)
        resp = self._response()
        self.streaming = resp.code == pb.ResponseCode.REPLICATE
        return resp

    def continue_(self, c_scn=None, c_idx=None, scn=None):
        r = self._req(pb.RequestCode.CONTINUE)
        if c_scn is not None:
            r.c_scn = c_scn
            if c_idx is not None:
                r.c_idx = c_idx
        elif scn is not None:
            r.scn = scn
        self._send(r)
        resp = self._response()
        self.streaming = resp.code == pb.ResponseCode.REPLICATE
        return resp

    def confirm_raw(self, c_scn, c_idx):
        r = self._req(pb.RequestCode.CONFIRM)
        r.c_scn = c_scn
        r.c_idx = c_idx
        self._send(r)

    def recv(self, timeout=None):
        """Next data message as (raw text, parsed dict); None on timeout."""
        old = self.sock.gettimeout()
        self.sock.settimeout(timeout if timeout is not None else self.timeout)
        try:
            (length,) = struct.unpack("<I", self._recv_exact(4))
        except socket.timeout:
            return None
        finally:
            self.sock.settimeout(old)
        if length == MAX_LENGTH:
            (length,) = struct.unpack("<Q", self._recv_exact(8))
        raw = self._recv_exact(length).decode("utf-8")
        return raw, json.loads(raw, parse_float=decimal.Decimal)

    # ------------------------------------------------------------- Debezium behaviour
    def connect(self, host, port, scn, idx=None):
        """OlrNetworkClient.connect/startFrom. Returns (path, response code name)."""
        self.connect_socket(host, port)
        self.skip_to_start_scn = scn if idx is None else None
        resp = self.info()
        if resp.code == pb.ResponseCode.REPLICATE:
            path = "CONTINUE"
            resp = self.continue_(c_scn=scn, c_idx=idx) if idx is not None else self.continue_(scn=scn)
        elif resp.code == pb.ResponseCode.READY:
            path = "START"
            resp = self.start(scn=scn)
        else:
            return "INFO", pb.ResponseCode.Name(resp.code)
        return path, pb.ResponseCode.Name(resp.code)

    def confirm(self, scn, idx):
        """Debezium 3.6.1: CONFIRM only when the SCN strictly increases."""
        sent = False
        if self.prev_scn != 0 and self.prev_scn < scn and idx is not None:
            self.confirm_raw(scn, idx)
            sent = True
        self.prev_scn = scn
        return sent

    def read_event(self, timeout=None):
        """readEvent incl. the skipToStartScn filter. Returns (raw, msg, skipped_list) or None."""
        skipped = []
        while True:
            got = self.recv(timeout)
            if got is None:
                return None if not skipped else (None, None, skipped)
            raw, msg = got
            if self.skip_to_start_scn is not None and "scn" in msg and int(msg["scn"]) < self.skip_to_start_scn:
                self.confirm(int(msg.get("c_scn", 0)), msg.get("c_idx"))
                skipped.append(raw)
                continue
            self.skip_to_start_scn = None
            return raw, msg, skipped
