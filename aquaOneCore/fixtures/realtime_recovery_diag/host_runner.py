"""Bounded physical runner for the isolated Core recovery fixture.

Never targets production Luma routes and never uploads firmware.
"""
import argparse
import base64
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import socket
import struct
import threading
import time
import urllib.error
import urllib.request

import serial
from serial.tools import list_ports


SCENARIOS = tuple(f"R{i}" for i in range(1, 11))
MAX_FRAME = 4096


class FixtureError(RuntimeError):
    pass


class RawWebSocket:
    def __init__(self, host, port, *, slow=False):
        self.sock = socket.create_connection((host, port), timeout=4)
        if slow:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        self.sock.settimeout(4)
        self.pending = b""
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            f"GET /ws/hil HTTP/1.1\r\nHost: {host}:{port}\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii")
        self.sock.sendall(request)
        while b"\r\n\r\n" not in self.pending:
            chunk = self.sock.recv(1024)
            if not chunk:
                raise FixtureError("WS handshake closed")
            self.pending += chunk
            if len(self.pending) > MAX_FRAME:
                raise FixtureError("WS headers too large")
        headers, self.pending = self.pending.split(b"\r\n\r\n", 1)
        if not headers.startswith(b"HTTP/1.1 101"):
            raise FixtureError(f"WS handshake: {headers.splitlines()[0]!r}")
        accept = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
        ).digest())
        if accept not in headers:
            raise FixtureError("WS accept mismatch")
        self.start = self.text()
        if self.start.get("type") != "stream_start":
            raise FixtureError(f"first WS message is not StreamStart: {self.start}")

    def read(self, length):
        while len(self.pending) < length:
            chunk = self.sock.recv(max(256, length - len(self.pending)))
            if not chunk:
                raise EOFError("WS TCP closed")
            self.pending += chunk
        result, self.pending = self.pending[:length], self.pending[length:]
        return result

    def frame(self, timeout=4):
        self.sock.settimeout(timeout)
        first, second = self.read(2)
        if not first & 0x80 or first & 0x70 or second & 0x80:
            raise FixtureError("invalid server WS FIN/RSV/mask")
        length = second & 0x7F
        if length == 126:
            length = struct.unpack("!H", self.read(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self.read(8))[0]
        if length > MAX_FRAME:
            raise FixtureError("unbounded server WS frame")
        return first & 0x0F, self.read(length)

    def text(self, timeout=4):
        opcode, payload = self.frame(timeout)
        if opcode != 1:
            raise FixtureError(f"expected TEXT, got opcode {opcode}")
        return json.loads(payload.decode("utf-8"))

    def ping(self):
        payload = b"diag"
        mask = os.urandom(4)
        self.sock.sendall(b"\x89\x84" + mask + bytes(
            byte ^ mask[i % 4] for i, byte in enumerate(payload)))
        if self.frame() != (10, payload):
            raise FixtureError("healthy client did not answer PING")

    def wait_closed(self, timeout=2.0):
        deadline = time.monotonic() + timeout
        for _ in range(16):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return False
            try:
                self.frame(timeout=remaining)
            except (EOFError, ConnectionResetError):
                return True
            except socket.timeout:
                return False
        return False

    def close(self):
        self.sock.close()


def position(marker):
    point = marker["position"]
    if point == {"kind": "before_first"}:
        return 0
    if point.get("kind") != "at" or not re.fullmatch(r"[1-9][0-9]*", point["sequence"]):
        raise FixtureError(f"invalid position: {point}")
    return int(point["sequence"])


class Runner:
    def __init__(self, host, port, com, timeout):
        self.host, self.port, self.com, self.timeout = host, port, com, timeout
        self.base = f"http://{host}:{port}"
        self.serial_lines = []
        self.serial_stop = threading.Event()
        self.serial_port = None
        self.serial_thread = None
        matching = [p for p in list_ports.comports()
                    if p.device.casefold() == com.casefold()]
        if len(matching) != 1:
            raise FixtureError(f"COM port {com} not present or ambiguous")
        self.port_identity = {
            "device": matching[0].device, "description": matching[0].description,
            "vid": matching[0].vid, "pid": matching[0].pid,
            "serial_number": matching[0].serial_number,
        }

    def __enter__(self):
        self.serial_port = serial.Serial(self.com, 115200, timeout=0.2)
        # This CH9102 board does not reset merely by opening COM6. Request a
        # normal EN reset so HIL_READY and fault logs belong to this run.
        self.serial_port.dtr = False
        self.serial_port.rts = True
        time.sleep(0.2)
        self.serial_port.rts = False

        def collect():
            while not self.serial_stop.is_set():
                data = self.serial_port.readline().decode("utf-8", "replace").strip()
                if data:
                    self.serial_lines.append(data)
                    if len(self.serial_lines) > 1200:
                        del self.serial_lines[:200]

        self.serial_thread = threading.Thread(target=collect, daemon=True)
        self.serial_thread.start()
        deadline = time.monotonic() + min(self.timeout, 35)
        while time.monotonic() < deadline:
            if any("HIL_READY" in line for line in self.serial_lines):
                try:
                    self.get("/api/diag/status")
                    return self
                except (OSError, urllib.error.URLError):
                    pass
            time.sleep(0.2)
        self.__exit__(None, None, None)
        raise FixtureError("fixture HIL_READY and HTTP were not observed")

    def __exit__(self, *_):
        self.serial_stop.set()
        if self.serial_thread:
            self.serial_thread.join(timeout=1)
        if self.serial_port:
            self.serial_port.close()

    def request(self, path, body=None):
        request = urllib.request.Request(
            self.base + path, data=body,
            method="GET" if body is None else "POST",
            headers={} if body is None else {"Content-Type": "text/plain"},
        )
        with urllib.request.urlopen(request, timeout=3) as response:
            payload = response.read(8192)
            if response.read(1):
                raise FixtureError(f"oversize response from {path}")
            return response.status, json.loads(payload)

    def get(self, path):
        status, value = self.request(path)
        if status != 200:
            raise FixtureError(f"{path}: HTTP {status}")
        return value

    def post(self, path, body=b""):
        status, value = self.request(path, body)
        if status != 202 or value.get("accepted") is not True:
            raise FixtureError(f"{path}: HTTP {status}: {value}")
        return value

    def wait_done(self, run, deadline):
        last = None
        while time.monotonic() < deadline:
            try:
                last = self.get("/api/diag/status")
                if last["run"] == run and last["phase"] == 7:
                    return last
                if last["run"] == run and last["phase"] == 8:
                    raise FixtureError(f"fixture scenario failed: {last}")
            except (OSError, urllib.error.URLError):
                pass  # The listener is intentionally down during recycle.
            time.sleep(0.15)
        raise FixtureError(f"scenario timeout; last diagnostics: {last}")

    def check_snapshot(self, start):
        snapshot = self.get("/api/hil/snapshot")
        if not snapshot["available"] or not snapshot["coherent"]:
            raise FixtureError(f"incoherent HTTP snapshot: {snapshot}")
        if int(snapshot["runtime"], 16) != int(start["runtime_id"], 16):
            raise FixtureError("HTTP/WS runtime mismatch")
        if snapshot["sequence"] != position(start):
            raise FixtureError("HTTP/WS position mismatch")
        return snapshot

    def scenario(self, name):
        before = self.get("/api/diag/status")
        before_status = self.get("/api/hil/status")
        first, healthy, reconnect = None, None, None
        frames = []
        started = time.monotonic()
        try:
            first = RawWebSocket(self.host, self.port)
            frames.append({"connection": "before", "frame": first.start})
            before_snapshot = self.check_snapshot(first.start)
            if name == "R4":
                healthy = RawWebSocket(self.host, self.port)
                frames.append({"connection": "healthy", "frame": healthy.start})
            run_response = self.post("/api/diag/run", name.encode("ascii"))
            done = self.wait_done(before["run"] + 1,
                                  time.monotonic() + self.timeout)
            after_status = self.get("/api/hil/status")
            reconnect = RawWebSocket(self.host, self.port)
            frames.append({"connection": "reconnect", "frame": reconnect.start})
            snapshot = self.check_snapshot(reconnect.start)
            reconnect.ping()
            if not first.wait_closed():
                raise FixtureError("old/offending WS session survived recovery or recycle")
            if done["result"] != 1 or not done["running"] or done["recovery"]:
                raise FixtureError("supervisor did not return healthy")
            if name in ("R3", "R6", "R7", "R8", "R9", "R10") and \
                    after_status["sequence"] != before_status["sequence"]:
                raise FixtureError("recycle/recovery issued artificial sequence")
            if name == "R1" and after_status["busy"] <= before_status["busy"]:
                raise FixtureError("R1 did not obtain actual Busy")
            if name == "R2" and (done["injected"]["queue"] <= before["injected"]["queue"] or
                                 after_status["queueFailure"] <= before_status["queueFailure"]):
                raise FixtureError("R2 did not obtain QueueWorkFailure")
            if name in ("R1", "R2", "R3") and \
                    done["cleared"] <= before["cleared"]:
                raise FixtureError(f"{name} did not clear sticky recovery")
            if name == "R4":
                trace = self.get("/api/diag/status")["r4"]
                if (done["injected"]["send"] <= before["injected"]["send"] or
                    done["injected"]["close"] != before["injected"]["close"] + 2 or
                    done["serviceProgresses"] <= before["serviceProgresses"] or
                    done["serviceRetries"] <= before["serviceRetries"]):
                    raise FixtureError("R4 did not exercise Closing/retry")
                if (not trace["liveBeforeSend"] or not trace["closingAtClose"] or
                    trace["fd"] < 0 or trace["slot"] >= 7 or
                    int(trace["generation"]) == 0 or
                    trace["closeAttempts"] < 3 or
                    trace["closeResults"][:3] != [-1, -1, 0] or
                    not trace["closeRequested"] or not trace["released"]):
                    raise FixtureError(f"R4 slot lifecycle incomplete: {trace}")
                if (done["recovery"] or after_status["recovery"] or
                    done["republishAttempts"] != before["republishAttempts"] or
                    done["completedCycles"] != before["completedCycles"] or
                    after_status["sequence"] != before_status["sequence"] + 1):
                    raise FixtureError("R4 changed global recovery or cohort position")
                event = healthy.text(timeout=3)
                frames.append({"connection": "healthy", "frame": event})
                if event.get("type") != "notification" or \
                        event.get("kind") != "luma_status_changed" or \
                        event.get("runtime_id") != reconnect.start["runtime_id"] or \
                        int(event["sequence"]) != snapshot["sequence"]:
                    raise FixtureError(f"healthy peer missed notification: {event}")
                healthy.ping()
            if name == "R5":
                if (done["serviceRetries"] <= before["serviceRetries"] or
                    done["stopAttempts"] <= before["stopAttempts"] or
                    done["serviceRecycleResults"] <= before["serviceRecycleResults"] or
                    done["handledRecycleSuggestions"] <= before["handledRecycleSuggestions"] or
                    done["stopProbeResult"] != 3):
                    raise FixtureError("R5 did not observe RetryNeeded/recycle suggestion")
            if name == "R6" and (done["injected"]["stop"] <= before["injected"]["stop"] or
                                 done["stopAttempts"] < before["stopAttempts"] + 2):
                raise FixtureError("R6 stop failure or retry absent")
            if name == "R7" and (done["republishAttempts"] < before["republishAttempts"] + 2 or
                                 done["injected"]["build"] <= before["injected"]["build"]):
                raise FixtureError("R7 republish failure or retry absent")
            if name == "R8" and (done["injected"]["begin"] <= before["injected"]["begin"] or
                                 done["beginAttempts"] < before["beginAttempts"] + 2):
                raise FixtureError("R8 begin failure or retry absent")
            if name in ("R5", "R6", "R7", "R8", "R9", "R10") and \
                    done["completedCycles"] <= before["completedCycles"]:
                raise FixtureError("HTTPD did not actually recycle")
            retry_metric = {"R6": "stop", "R7": "republish", "R8": "begin"}.get(name)
            if retry_metric and not 950 <= done["retryGapMs"][retry_metric] <= 5000:
                raise FixtureError(f"{name} retry not spaced near 1 s")
            for point in ("beforeHeap", "afterHeap", "stableHeap"):
                if not all(value > 0 for value in done[point]):
                    raise FixtureError(f"missing {point} heap sample")
            return {
                "verdict": "PASS", "seconds": round(time.monotonic() - started, 3),
                "before": before, "after": done,
                "before_status": before_status, "after_status": after_status,
                "before_snapshot": before_snapshot,
                "run_response": run_response, "ws_text_frames": frames,
                "snapshot": snapshot, "stream_start": reconnect.start,
                "r4_trace": trace if name == "R4" else None,
                "metrics": {bucket: self.get(f"/api/diag/{bucket}-metrics")
                            for bucket in ("baseline", "recovery", "recycle")},
            }
        finally:
            for client in (first, healthy, reconnect):
                if client:
                    client.close()

    def heap_survey(self):
        samples = {}
        sockets = []
        try:
            samples["zero_clients"] = self.get("/api/diag/status")["heap"]
            sockets.append(RawWebSocket(self.host, self.port))
            samples["one_client"] = self.get("/api/diag/status")["heap"]
            sockets.append(RawWebSocket(self.host, self.port))
            samples["two_clients"] = self.get("/api/diag/status")["heap"]
            sockets.append(RawWebSocket(self.host, self.port, slow=True))
            for _ in range(3):
                self.post("/api/diag/change")
                time.sleep(0.2)
            samples["nonreading_client"] = self.get("/api/diag/status")["heap"]
        finally:
            for ws in sockets:
                ws.close()
        time.sleep(0.5)
        for _ in range(5):
            ws = RawWebSocket(self.host, self.port)
            ws.close()
        samples["after_reconnects"] = self.get("/api/diag/status")["heap"]
        return {"verdict": "PASS", "samples": samples}

    def check_serial(self):
        bad = [line for line in self.serial_lines
               if re.search(r"Guru Meditation|watchdog|brownout|panic", line, re.I)]
        if bad:
            raise FixtureError(f"serial fault markers: {bad[-5:]}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--tcp-port", type=int, required=True)
    parser.add_argument("--com-port", required=True)
    parser.add_argument("--timeout", type=float, default=38)
    parser.add_argument("--scenario", choices=SCENARIOS + ("HEAP", "ALL"), required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not 1 <= args.tcp_port <= 65535 or not 15 <= args.timeout <= 90:
        parser.error("TCP port or timeout outside bounded range")
    try:
        if ipaddress.ip_address(args.host).version != 4:
            parser.error("fixture requires an explicit IPv4 address")
    except ValueError:
        parser.error("--host must be an explicit IPv4 address")
    reports = Path(__file__).resolve().parent / "reports"
    reports.mkdir(exist_ok=True)
    output = args.output or reports / f"diag_{time.strftime('%Y%m%d_%H%M%S')}.json"
    if output.resolve().parent != reports or output.exists():
        parser.error("output must be a new file directly under ignored reports/")
    result = {"fixture": "F10.3B-HIL-2-DIAG", "host": args.host,
              "tcp_port": args.tcp_port, "com_port": args.com_port,
              "scenarios": {name: {"verdict": "NOT RUN"} for name in SCENARIOS},
              "heap_survey": {"verdict": "NOT RUN"}, "verdict": "NOT RUN"}
    try:
        with Runner(args.host, args.tcp_port, args.com_port, args.timeout) as runner:
            result["port_identity"] = runner.port_identity
            selected = SCENARIOS if args.scenario == "ALL" else (args.scenario,)
            if args.scenario in ("HEAP", "ALL"):
                result["heap_survey"] = runner.heap_survey()
            for name in selected:
                if name == "HEAP":
                    continue
                try:
                    if name == "R10":
                        cycles = [runner.scenario(name) for _ in range(3)]
                        result["scenarios"][name] = {"verdict": "PASS", "cycles": cycles}
                    else:
                        result["scenarios"][name] = runner.scenario(name)
                    print(f"{name}: PASS")
                except Exception as exc:
                    result["scenarios"][name] = {
                        "verdict": "FAIL", "error": f"{type(exc).__name__}: {exc}"}
                    raise
            runner.check_serial()
            result["serial_tail"] = runner.serial_lines[-100:]
            result["verdict"] = "PASS"
    except Exception as exc:
        result["verdict"] = "FAIL"
        result["error"] = f"{type(exc).__name__}: {exc}"
    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"{result['verdict']}: {output}")
    if result["verdict"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
