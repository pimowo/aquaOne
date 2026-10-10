"""Bounded control-frame HIL against production Luma; no result files or NVS writes."""
import argparse
import base64
import hashlib
import json
import os
import re
import socket
import struct
import time
import urllib.request


class RawWebSocket:
    def __init__(self, host):
        self.sock = socket.create_connection((host, 80), timeout=4)
        self.sock.settimeout(4)
        self.buffer = b""
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        self.sock.sendall((
            f"GET /ws/realtime HTTP/1.1\r\nHost: {host}\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii"))
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.sock.recv(2048)
            if not chunk:
                raise AssertionError("handshake closed")
            self.buffer += chunk
            if len(self.buffer) > 4096:
                raise AssertionError("handshake too large")
        headers, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        assert headers.startswith(b"HTTP/1.1 101 "), headers
        accept = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
        ).digest())
        assert accept in headers
        self.start = self.json_frame()
        assert self.start["type"] == "stream_start"
        assert re.fullmatch(r"[0-9A-F]{16}", self.start["runtime_id"])
        assert int(self.start["runtime_id"], 16) != 0
        position_sequence(self.start["position"])

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.sock.close()

    def read(self, count):
        while len(self.buffer) < count:
            chunk = self.sock.recv(max(256, count - len(self.buffer)))
            if not chunk:
                raise EOFError("peer closed")
            self.buffer += chunk
        output, self.buffer = self.buffer[:count], self.buffer[count:]
        return output

    def receive(self):
        first, second = self.read(2)
        assert first & 0x80 and not first & 0x70, "invalid server FIN/RSV"
        assert not second & 0x80, "masked server frame"
        length = second & 0x7F
        if length == 126:
            length = struct.unpack("!H", self.read(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self.read(8))[0]
        assert length <= 4096, "unbounded server frame"
        return first & 0x0F, self.read(length)

    def json_frame(self):
        opcode, payload = self.receive()
        assert opcode == 1, f"expected TEXT, received opcode {opcode}"
        return json.loads(payload.decode("utf-8"))

    def send(self, opcode, payload=b"", *, final=True, masked=True):
        header = bytes([(0x80 if final else 0) | opcode])
        length = len(payload)
        mask_bit = 0x80 if masked else 0
        if length < 126:
            header += bytes([mask_bit | length])
        else:
            header += bytes([mask_bit | 126]) + struct.pack("!H", length)
        if masked:
            mask = os.urandom(4)
            header += mask
            payload = bytes(value ^ mask[i % 4] for i, value in enumerate(payload))
        self.sock.sendall(header + payload)

    def ping(self, payload):
        started = time.monotonic()
        self.send(9, payload)
        assert self.receive() == (10, payload), "PONG opcode/payload mismatch"
        return round((time.monotonic() - started) * 1000, 1)

    def close_handshake(self, payload):
        self.send(8, payload)
        assert self.receive() == (8, payload), "CLOSE opcode/payload mismatch"
        assert self.sock.recv(1) == b"", "TCP still open after CLOSE"


def position_sequence(position):
    if position == {"kind": "before_first"}:
        return 0
    assert set(position) == {"kind", "sequence"} and position["kind"] == "at"
    sequence = position["sequence"]
    assert isinstance(sequence, str) and re.fullmatch(r"[1-9][0-9]*", sequence)
    assert int(sequence) <= 0xFFFFFFFFFFFFFFFF
    return int(sequence)


class Harness:
    def __init__(self, host):
        self.host = host
        self.base = f"http://{host}"

    def request(self, path, data=None):
        body = None if data is None else json.dumps(data).encode("utf-8")
        request = urllib.request.Request(
            self.base + path, data=body,
            headers={} if body is None else {"Content-Type": "application/json"},
            method="GET" if body is None else "POST",
        )
        with urllib.request.urlopen(request, timeout=5) as response:
            assert response.status == 200
            return json.load(response)

    def status(self):
        return self.request("/api/lumasense/status")

    def mode(self, value):
        result = self.request("/api/lumasense/mode", {"mode": value})
        assert result["ok"] and result["result"] in ("applied", "no_change")

    def control_roundtrips(self):
        baseline = self.status()["realtime"]
        with RawWebSocket(self.host) as ws:
            assert ws.start["runtime_id"] == baseline["runtime_id"]
            assert ws.start["position"] == baseline["position"]
            times = [ws.ping(payload) for payload in (b"hil2", b"", bytes(range(125)))]
            for i in range(3):
                times.append(ws.ping(bytes([i])))
            ws.send(10, b"unsolicited pong")
            # A following PING is a barrier: PONG must not produce another reply.
            times.append(ws.ping(b"after pong"))
            assert self.status()["realtime"] == baseline
            ws.close_handshake(struct.pack("!H", 1000) + "HIL \u0105".encode("utf-8"))
        with RawWebSocket(self.host) as ws:
            assert ws.start["position"] == baseline["position"]
            ws.close_handshake(b"")
        assert self.status()["realtime"] == baseline
        print("PASS PING payload/empty/125B/repeated, PONG, CLOSE payload/empty, reconnect; RTT ms", times)

    def notification_and_survivor(self):
        with RawWebSocket(self.host) as a, RawWebSocket(self.host) as b:
            initial = self.status()["realtime"]
            sequence = position_sequence(initial["position"])
            a.ping(b"before notification")
            b.send(10, b"pong")
            self.mode("SERVICE")
            for ws in (a, b):
                event = ws.json_frame()
                assert event == {"type": "notification", "runtime_id": initial["runtime_id"],
                                 "sequence": str(sequence + 1), "kind": "luma_status_changed"}
            status = self.status()
            assert status["mode"] == "SERVICE"
            assert position_sequence(status["realtime"]["position"]) == sequence + 1
            b.close_handshake(struct.pack("!H", 1000))
            with RawWebSocket(self.host) as replacement:
                assert replacement.start["runtime_id"] == initial["runtime_id"]
                assert position_sequence(replacement.start["position"]) == sequence + 1
                self.mode("NORMAL")
                for ws in (a, replacement):
                    event = ws.json_frame()
                    assert event["type"] == "notification" and event["kind"] == "luma_status_changed"
                    assert event["runtime_id"] == initial["runtime_id"]
                    assert event["sequence"] == str(sequence + 2)
                assert position_sequence(self.status()["realtime"]["position"]) == sequence + 2
                replacement.close_handshake(b"")
            a.close_handshake(b"")
        print("PASS two clients, notification after PING, CLOSE survivor, reconnect; sequence", sequence, "->", sequence + 2)

    def rejected_frames(self):
        baseline = self.status()["realtime"]
        cases = [
            ("fragmented PING", 9, b"", {"final": False}),
            ("126B PING", 9, bytes(126), {}),
            ("unmasked PING", 9, b"hil2", {"masked": False}),
            ("one-byte CLOSE", 8, b"x", {}),
            ("invalid CLOSE code", 8, struct.pack("!H", 1005), {}),
            ("invalid CLOSE UTF8", 8, struct.pack("!H", 1000) + b"\xc0\x80", {}),
            ("unsupported TEXT", 1, b"{}", {}),
            ("unsupported BINARY", 2, b"\x00", {}),
        ]
        with RawWebSocket(self.host) as healthy:
            for name, opcode, payload, options in cases:
                with RawWebSocket(self.host) as invalid:
                    invalid.send(opcode, payload, **options)
                    try:
                        reply = invalid.receive()
                        assert reply[0] == 8, f"invalid frame accepted: {name}"
                    except (EOFError, ConnectionResetError):
                        pass
                healthy.ping(b"healthy")
                assert self.status()["realtime"] == baseline
                print("PASS rejected", name, "; healthy session and sequence preserved")
            healthy.close_handshake(b"")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    args = parser.parse_args()
    harness = Harness(args.host)
    system = harness.request("/api/system")
    assert system["device_type"] == "luma" and system["api_protocol_version"] == {"major": 1, "minor": 1}
    initial = harness.status()
    assert initial["mode"] == "NORMAL" and initial["activeProfile"] == 1, "requires NORMAL/profile 1 test board"
    started = time.monotonic()
    try:
        harness.control_roundtrips()
        harness.notification_and_survivor()
        harness.rejected_frames()
    finally:
        if harness.status()["mode"] != "NORMAL":
            harness.mode("NORMAL")
    final = harness.status()
    assert final["mode"] == "NORMAL" and final["activeProfile"] == 1
    assert harness.request("/api/system")["device_id"] == system["device_id"]
    print("PASS final HTTP 200, NORMAL/profile 1, stable DeviceId; elapsed s", round(time.monotonic() - started, 2))


if __name__ == "__main__":
    main()
