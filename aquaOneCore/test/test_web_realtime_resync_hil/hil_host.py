#!/usr/bin/env python3
import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import threading
import time

import requests


class HilFailure(RuntimeError):
    pass


class RawWebSocket:
    def __init__(self, host, timeout=3.0, receive_buffer=None):
        self.host = host
        self.sock = socket.create_connection((host, 80), timeout=timeout)
        if receive_buffer:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
        self.sock.settimeout(timeout)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            "GET /ws/hil HTTP/1.1\r\n"
            f"Host: {host}\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii")
        self.sock.sendall(request)
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise HilFailure("WS handshake closed")
            data += chunk
        headers, self.buffer = data.split(b"\r\n\r\n", 1)
        if not headers.startswith(b"HTTP/1.1 101"):
            raise HilFailure(f"WS handshake failed: {headers.splitlines()[0]!r}")
        expected = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
        ).digest())
        if expected.lower() not in headers.lower():
            raise HilFailure("WS accept mismatch")

    def _read_exact(self, count):
        while len(self.buffer) < count:
            chunk = self.sock.recv(max(4096, count - len(self.buffer)))
            if not chunk:
                raise EOFError("WS closed")
            self.buffer += chunk
        result, self.buffer = self.buffer[:count], self.buffer[count:]
        return result

    def receive(self, timeout=3.0):
        self.sock.settimeout(timeout)
        first, second = self._read_exact(2)
        opcode = first & 0x0F
        length = second & 0x7F
        if length == 126:
            length = struct.unpack("!H", self._read_exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._read_exact(8))[0]
        if second & 0x80:
            mask = self._read_exact(4)
            payload = bytearray(self._read_exact(length))
            for i in range(length):
                payload[i] ^= mask[i % 4]
            payload = bytes(payload)
        else:
            payload = self._read_exact(length)
        if opcode == 8:
            raise EOFError("WS close frame")
        if opcode != 2:
            raise HilFailure(f"unexpected opcode {opcode}")
        return parse_frame(payload)

    def close(self):
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()


def parse_frame(payload):
    if len(payload) == 18 and payload[0] == 1:
        return {
            "type": "start",
            "runtime": f"{struct.unpack_from('<Q', payload, 1)[0]:016x}",
            "kind": "before-first" if payload[9] == 1 else "at",
            "sequence": struct.unpack_from("<Q", payload, 10)[0],
        }
    if len(payload) == 256 and payload[0] == 2:
        return {
            "type": "notification",
            "runtime": f"{struct.unpack_from('<Q', payload, 1)[0]:016x}",
            "sequence": struct.unpack_from("<Q", payload, 9)[0],
            "revision": struct.unpack_from("<I", payload, 17)[0],
            "checksum": struct.unpack_from("<I", payload, 21)[0],
        }
    raise HilFailure(f"unknown frame length/type: {len(payload)}/{payload[:1]!r}")


class Harness:
    def __init__(self, host):
        self.host = host
        self.base = f"http://{host}"
        self.session = requests.Session()
        self.latencies = {name: [] for name in ("http_only", "one_ws", "two_ws", "slow", "recovery")}
        self.results = {"host": host, "scenarios": {}, "heap": {}}

    def get(self, path, bucket=None, allow_503=False):
        started = time.perf_counter()
        response = self.session.get(self.base + path, timeout=4)
        elapsed = (time.perf_counter() - started) * 1000.0
        if bucket:
            self.latencies[bucket].append(elapsed)
        if response.status_code == 503 and allow_503:
            return response.json()
        response.raise_for_status()
        return response.json()

    def post(self, path):
        response = self.session.post(self.base + path, timeout=5)
        response.raise_for_status()
        return response.json()

    def status(self, bucket=None):
        return self.get("/api/hil/status", bucket)

    def snapshot(self, bucket=None, allow_503=False):
        data = self.get("/api/hil/snapshot", bucket, allow_503)
        if data.get("available"):
            if data["inverse"] != ((~data["value"]) & 0xFFFFFFFF):
                raise HilFailure("snapshot inverse mismatch")
            expected = data["revision"] ^ data["value"] ^ data["inverse"] ^ 0xA55A3CC3
            if data["checksum"] != expected:
                raise HilFailure("snapshot checksum mismatch")
        return data

    def connect(self, slow=False):
        ws = RawWebSocket(self.host, receive_buffer=512 if slow else None)
        first = ws.receive()
        if first["type"] != "start":
            raise HilFailure("notification before StreamStart")
        return ws, first

    def wait_status(self, predicate, timeout=8.0):
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            try:
                last = self.status("recovery")
                if predicate(last):
                    return last
            except requests.RequestException:
                pass
            time.sleep(0.05)
        raise HilFailure(f"status timeout; last={last}")

    @staticmethod
    def assert_sequence(events, start, runtime):
        expected = start
        for event in events:
            if event["type"] != "notification" or event["runtime"] != runtime:
                raise HilFailure(f"invalid notification {event}")
            if event["sequence"] != expected:
                raise HilFailure(f"sequence gap: expected {expected}, got {event['sequence']}")
            expected += 1

    def heap_sample(self, name):
        status = self.status()
        self.results["heap"][name] = {
            "free_internal": status["heapFreeInternal"],
            "min_internal": status["heapMinInternal"],
            "largest_internal": status["heapLargestInternal"],
        }
        return status

    def recover(self, previous_sequence):
        self.post("/api/hil/recover")
        status = self.wait_status(lambda s: not s["recovery"], timeout=12)
        if status["sequence"] != previous_sequence:
            raise HilFailure("recovery issued a new sequence")
        snap = self.snapshot("recovery")
        if snap["sequence"] != previous_sequence:
            raise HilFailure("recovery snapshot position mismatch")
        ws, marker = self.connect()
        if marker["sequence"] != previous_sequence:
            raise HilFailure("recovery StreamStart mismatch")
        return ws, marker, status

    def run(self):
        h0 = self.heap_sample("H1_server_started")
        runtime = h0["runtime"]
        snap0 = self.snapshot("http_only")
        if snap0["runtime"] != runtime or snap0["kind"] != "before-first":
            raise HilFailure("baseline snapshot is not BeforeFirst")
        ws, start = self.connect()
        if start["runtime"] != runtime or start["kind"] != "before-first":
            raise HilFailure("baseline StreamStart is not BeforeFirst")
        self.results["scenarios"]["baseline"] = {"snapshot": snap0, "marker": start}
        self.heap_sample("H2_one_ws")

        events = []
        for _ in range(5):
            self.post("/api/hil/change")
            events.append(ws.receive())
        self.assert_sequence(events, 1, runtime)
        snap = self.snapshot("one_ws")
        if snap["sequence"] != 5:
            raise HilFailure("one-client snapshot mismatch")
        self.results["scenarios"]["one_client"] = {"notifications": len(events), "last": events[-1]}

        overlap_events = []
        def overlap_changes():
            for _ in range(8):
                self.post("/api/hil/change")
                time.sleep(0.01)
        worker = threading.Thread(target=overlap_changes)
        worker.start()
        overlap_snapshot = self.snapshot("one_ws")
        while worker.is_alive() or len(overlap_events) < 8:
            overlap_events.append(ws.receive())
            if len(overlap_events) >= 8:
                break
        worker.join()
        self.assert_sequence(overlap_events, 6, runtime)
        applied = [e for e in overlap_events if e["sequence"] > overlap_snapshot["sequence"]]
        if applied and applied[0]["sequence"] != overlap_snapshot["sequence"] + 1:
            raise HilFailure("overlap apply gap")
        self.results["scenarios"]["overlap"] = {
            "snapshot_sequence": overlap_snapshot["sequence"],
            "buffered": len(overlap_events), "applied_after_discard": len(applied)
        }

        ws.close()
        time.sleep(0.15)
        ws, reconnect_marker = self.connect()
        reconnect_snapshot = self.snapshot("one_ws")
        if reconnect_snapshot["sequence"] < reconnect_marker["sequence"]:
            raise HilFailure("reconnect accepted snapshot older than marker")
        self.results["scenarios"]["reconnect"] = reconnect_marker

        ws_b, marker_b = self.connect()
        self.heap_sample("H3_two_ws")
        two_a, two_b = [], []
        base = reconnect_marker["sequence"] + 1
        for _ in range(3):
            self.post("/api/hil/change")
            two_a.append(ws.receive())
            two_b.append(ws_b.receive())
        self.assert_sequence(two_a, base, runtime)
        self.assert_sequence(two_b, base, runtime)
        self.snapshot("two_ws")
        ws.close()
        time.sleep(0.1)
        self.post("/api/hil/change")
        surviving = ws_b.receive()
        if surviving["sequence"] != base + 3:
            raise HilFailure("surviving client continuity lost")
        self.results["scenarios"]["two_clients"] = {"marker_b": marker_b, "survivor": surviving}

        ws_b.close()
        cycles = []
        for _ in range(15):
            cycle_ws, marker = self.connect()
            cycles.append(marker["sequence"])
            cycle_ws.close()
            time.sleep(0.03)
        self.results["scenarios"]["slot_cleanup"] = {"cycles": len(cycles), "markers": cycles}

        race_result = None
        race_ws, race_marker = self.connect()
        self.post("/api/hil/change")
        try:
            race_event = race_ws.receive(timeout=2)
            if race_event["type"] != "notification" or race_event["sequence"] <= race_marker["sequence"]:
                raise HilFailure("connecting race silently lost or reordered")
            race_result = "marker-then-notification"
        except EOFError:
            race_result = "closed-for-resync"
        race_ws.close()
        self.results["scenarios"]["connecting_race"] = race_result

        failure_ws, _ = self.connect()
        before_failure = self.status()["sequence"]
        self.post("/api/hil/fail-snapshot")
        failed = self.wait_status(lambda s: s["recovery"])
        spent = failed["sequence"]
        if spent != before_failure + 1:
            raise HilFailure("snapshot failure did not spend N")
        unavailable = self.snapshot(allow_503=True)
        if unavailable.get("available") is not False:
            raise HilFailure("failed snapshot remained available")
        try:
            failure_ws.receive(timeout=0.5)
            raise HilFailure("failed snapshot emitted notification")
        except (EOFError, socket.timeout):
            pass
        failure_ws.close()
        recovered_ws, recovered_marker, _ = self.recover(spent)
        self.results["scenarios"]["snapshot_failure"] = {
            "spent": spent, "recovered_marker": recovered_marker
        }

        self.post("/api/hil/fail-notification")
        notify_failed = self.wait_status(lambda s: s["recovery"])
        notify_n = notify_failed["sequence"]
        notify_snapshot = self.snapshot("recovery")
        if notify_snapshot["sequence"] != notify_n:
            raise HilFailure("notification failure cohort unavailable")
        recovered_ws.close()
        notify_ws, notify_marker, _ = self.recover(notify_n)
        self.results["scenarios"]["notification_failure"] = {
            "spent": notify_n, "recovered_marker": notify_marker
        }

        notify_ws.close()
        saturation_ws, saturation_marker = self.connect()
        busy_before = self.status()["busy"]
        self.post("/api/hil/blocked-burst")
        saturated = self.wait_status(lambda s: s["busy"] > busy_before)
        if not saturated["recovery"]:
            raise HilFailure("backpressure did not enter recovery")
        saturation_n = saturated["sequence"]
        first_boundary = saturated["firstBackpressureAttempt"]
        saturation_ws.close()
        post_sat_ws, post_sat_marker, _ = self.recover(saturation_n)
        self.results["scenarios"]["saturation"] = {
            "marker_before": saturation_marker, "sequence": saturation_n,
            "first_boundary": first_boundary, "result": "Busy",
            "marker_after": post_sat_marker
        }
        self.heap_sample("H5_saturation_recovered")

        post_sat_ws.close()
        slow_ws, _ = self.connect(slow=True)
        normal_ws, normal_marker = self.connect()
        normal_events = []
        reader_stop = threading.Event()
        def normal_reader():
            while not reader_stop.is_set():
                try:
                    normal_events.append(normal_ws.receive(timeout=0.5))
                except socket.timeout:
                    continue
                except (EOFError, OSError):
                    return
        reader = threading.Thread(target=normal_reader)
        reader.start()
        self.post("/api/hil/paced")
        slow_latencies = []
        deadline = time.monotonic() + 12
        slow_status = None
        while time.monotonic() < deadline:
            try:
                started = time.perf_counter()
                slow_status = self.status()
                slow_latencies.append((time.perf_counter() - started) * 1000.0)
                self.snapshot("slow", allow_503=True)
                if slow_status["pacedRemaining"] == 0 or slow_status["recovery"]:
                    break
            except requests.RequestException:
                pass
            time.sleep(0.1)
        reader_stop.set()
        reader.join(timeout=2)
        slow_ws.close()
        normal_ws.close()
        if normal_events:
            self.assert_sequence(normal_events, normal_marker["sequence"] + 1, runtime)
        self.results["scenarios"]["slow_client"] = {
            "normal_marker": normal_marker, "normal_frames": len(normal_events),
            "http_samples": len(slow_latencies),
            "max_http_ms": max(slow_latencies) if slow_latencies else None,
            "recovery": bool(slow_status and slow_status["recovery"])
        }
        if slow_status and slow_status["recovery"]:
            self.recover(slow_status["sequence"])[0].close()
        self.heap_sample("H6_clients_closed")

        stable_ws, stable_marker = self.connect()
        stability_start = time.monotonic()
        stability_notifications = 0
        stability_requests = 0
        stability_reconnects = 0
        while time.monotonic() - stability_start < 60.0:
            self.post("/api/hil/change")
            stability_requests += 1
            event = stable_ws.receive(timeout=3)
            if event["type"] != "notification":
                raise HilFailure("stability non-notification")
            stability_notifications += 1
            self.snapshot("one_ws")
            stability_requests += 1
            if stability_notifications % 20 == 0:
                stable_ws.close()
                stable_ws, marker = self.connect()
                if marker["runtime"] != runtime:
                    raise HilFailure("runtime changed without restart")
                stability_reconnects += 1
            time.sleep(0.35)
        stable_ws.close()
        self.results["scenarios"]["stability"] = {
            "duration_s": time.monotonic() - stability_start,
            "requests": stability_requests,
            "notifications": stability_notifications,
            "reconnects": stability_reconnects
        }
        self.heap_sample("H7_final")

        pre_restart = self.status()
        self.results["final_status_before_restart"] = pre_restart
        self.post("/api/hil/restart")
        deadline = time.monotonic() + 25
        post_restart = None
        while time.monotonic() < deadline:
            try:
                candidate = self.status()
                if candidate["runtime"] != pre_restart["runtime"]:
                    post_restart = candidate
                    break
            except requests.RequestException:
                pass
            time.sleep(0.25)
        if post_restart is None:
            raise HilFailure("runtime restart proof timed out")
        restart_snapshot = self.snapshot("http_only")
        restart_ws, restart_marker = self.connect()
        restart_ws.close()
        if restart_snapshot["kind"] != "before-first" or restart_marker["kind"] != "before-first":
            raise HilFailure("restart did not reset stream")
        self.results["scenarios"]["runtime_restart"] = {
            "before": pre_restart["runtime"], "after": post_restart["runtime"],
            "snapshot": restart_snapshot, "marker": restart_marker
        }

        self.results["latency_ms"] = {}
        for name, values in self.latencies.items():
            if values:
                ordered = sorted(values)
                self.results["latency_ms"][name] = {
                    "count": len(values), "min": min(values),
                    "median": ordered[len(ordered) // 2], "max": max(values)
                }
        return self.results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    harness = Harness(args.host)
    try:
        result = harness.run()
        result["verdict"] = "PASS"
    except Exception as exc:
        result = harness.results
        result["verdict"] = "FAIL"
        result["error"] = f"{type(exc).__name__}: {exc}"
        with open(args.output, "w", encoding="utf-8") as handle:
            json.dump(result, handle, indent=2)
        raise
    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print(json.dumps({"verdict": result["verdict"], "output": args.output}))


if __name__ == "__main__":
    main()
