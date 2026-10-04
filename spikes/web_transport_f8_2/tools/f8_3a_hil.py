"""Small standard-library HIL driver for the isolated ESP-IDF Web spike."""
import concurrent.futures
import http.client
import json
import os
import socket
import statistics
import struct
import threading
import time
import urllib.parse

HOST = os.environ.get("F83A_HOST", "")
PORT = 80
SNAPSHOT = "/api/spike/snapshot"
BODY = "/api/spike/body"
NETWORK_LOSS = "/api/spike/hil/network-loss"
WS_PATH = "/ws/spike"
HARNESS_TIMEOUT_SECONDS = 360


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def http_request(method, path, body=None):
    connection = http.client.HTTPConnection(HOST, PORT, timeout=5)
    started = time.perf_counter()
    connection.request(method, path, body=body)
    response = connection.getresponse()
    payload = response.read()
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    result = (response.status, payload, elapsed_ms)
    connection.close()
    return result


def get_snapshot():
    status, payload, elapsed = http_request("GET", SNAPSHOT)
    check(status == 200, "snapshot HTTP status %s" % status)
    decoded = json.loads(payload.decode("utf-8"))
    check(set(decoded) == {"revision", "value"}, "snapshot fields %r" % decoded)
    return decoded, elapsed


def latency_summary(samples):
    return "min=%.2fms avg=%.2fms max=%.2fms n=%d" % (
        min(samples), statistics.mean(samples), max(samples), len(samples)
    )


class WebSocket:
    def __init__(self):
        self.sock = socket.create_connection((HOST, PORT), timeout=5)
        self.sock.settimeout(5)
        key = "dGhlIHNhbXBsZSBub25jZQ=="
        request = (
            "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        ) % (WS_PATH, HOST, PORT, key)
        self.sock.sendall(request.encode("ascii"))
        response = b""
        while b"\r\n\r\n" not in response:
            response += self.sock.recv(1024)
        header, self.pending = response.split(b"\r\n\r\n", 1)
        check(b" 101 " in header, "websocket handshake failed: %r" % header)

    def send_frame(self, opcode, payload=b""):
        if isinstance(payload, str):
            payload = payload.encode("utf-8")
        size = len(payload)
        first = bytes([0x80 | opcode])
        mask = os.urandom(4)
        if size < 126:
            header = first + bytes([0x80 | size])
        elif size < 65536:
            header = first + bytes([0x80 | 126]) + struct.pack("!H", size)
        else:
            header = first + bytes([0x80 | 127]) + struct.pack("!Q", size)
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def recv_frame(self):
        def read_exact(count):
            result = bytearray()
            while len(result) < count:
                if self.pending:
                    take = min(count - len(result), len(self.pending))
                    result.extend(self.pending[:take])
                    self.pending = self.pending[take:]
                else:
                    chunk = self.sock.recv(count - len(result))
                    if not chunk:
                        raise ConnectionError("websocket peer closed")
                    result.extend(chunk)
            return bytes(result)

        head = read_exact(2)
        opcode = head[0] & 0x0F
        length = head[1] & 0x7F
        if length == 126:
            length = struct.unpack("!H", read_exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", read_exact(8))[0]
        mask = read_exact(4) if head[1] & 0x80 else None
        payload = read_exact(length)
        if mask:
            payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        return opcode, payload

    def recv_json(self):
        opcode, payload = self.recv_frame()
        check(opcode == 1, "expected text frame, got opcode %d" % opcode)
        return json.loads(payload.decode("utf-8"))

    def close(self):
        try:
            self.sock.settimeout(1)
            self.send_frame(8, struct.pack("!H", 1000))
            opcode, _ = self.recv_frame()
            check(opcode == 8, "server did not acknowledge WebSocket close")
        finally:
            self.sock.close()


def connect_ws():
    client = WebSocket()
    greeting = client.recv_json()
    check(greeting.get("type") == "spike.changed", "bad WS greeting %r" % greeting)
    return client, greeting


def test_posts():
    expected = [(0, 400), (1, 200), (256, 200), (257, 413), (1024, 413)]
    results = []
    for size, expected_status in expected:
        status, _, _ = http_request("POST", BODY, b"x" * size)
        check(status == expected_status, "POST %d B expected %d, got %d" % (size, expected_status, status))
        results.append("%dB=%d" % (size, status))
    return ", ".join(results)


def wait_for_http(timeout=30):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            return get_snapshot()[0]
        except Exception as error:  # retry while DHCP/connectivity recovers
            last_error = error
            time.sleep(1)
    raise RuntimeError("HTTP did not recover: %r" % last_error)


def main():
    check(bool(HOST), "set F83A_HOST to the DHCP IP reported by the ESP32")
    timer = threading.Timer(HARNESS_TIMEOUT_SECONDS, lambda: os._exit(124))
    timer.daemon = True
    timer.start()
    results = []
    wait_for_http(30)
    # Idle snapshot and parallel burst, with latency captured client-side.
    idle_samples = []
    idle_state, elapsed = get_snapshot()
    idle_samples.append(elapsed)
    for _ in range(4):
        _, elapsed = get_snapshot()
        idle_samples.append(elapsed)
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        parallel = list(pool.map(lambda _: get_snapshot(), range(20)))
    idle_samples.extend(elapsed for _, elapsed in parallel)
    results.append(("HTTP only + 20 parallel GET", "PASS", latency_summary(idle_samples)))
    check(all(state["revision"] == idle_state["revision"] for state, _ in parallel), "idle snapshot state changed")

    a, greeting_a = connect_ws()
    results.append(("1 WS", "PASS", "handshake + greeting revision=%s" % greeting_a["revision"]))
    one_ws_samples = []
    for _ in range(20):
        _, elapsed = get_snapshot()
        one_ws_samples.append(elapsed)
    check(a.sock.fileno() >= 0, "WS A disconnected during HTTP")
    results.append(("HTTP + 1 WS", "PASS", latency_summary(one_ws_samples)))

    b, greeting_b = connect_ws()
    state_b, _ = get_snapshot()
    a.send_frame(1, "change-before-broadcast")
    event_a = a.recv_json()
    event_b = b.recv_json()
    check(event_a["revision"] == event_b["revision"], "sync/async revisions differ")
    check(event_a["revision"] == state_b["revision"] + 1, "WS event revision did not advance")
    results.append(("2 WS + sync/async send", "PASS", "A(sync), B(async), revision=%d" % event_a["revision"]))

    two_ws_samples = []
    for _ in range(20):
        _, elapsed = get_snapshot()
        two_ws_samples.append(elapsed)
    check(a.sock.fileno() >= 0 and b.sock.fileno() >= 0, "a WS disconnected during HTTP + 2 WS")
    results.append(("HTTP + 2 WS", "PASS", latency_summary(two_ws_samples)))

    a.send_frame(9, b"ping")
    opcode, pong = a.recv_frame()
    check(opcode == 10 and pong == b"ping", "expected WebSocket pong")
    results.append(("PING/PONG", "PASS", "server pong observed"))

    # A slow reader remains connected while another socket and HTTP continue.
    slow, _ = connect_ws()
    slow_count = 32
    slow_started = time.perf_counter()
    for index in range(slow_count):
        a.send_frame(1, "slow-client-%d" % index)
        a.recv_json()
    slow_send_seconds = time.perf_counter() - slow_started
    for _ in range(5):
        get_snapshot()
    slow_events = [slow.recv_json() for _ in range(slow_count)]
    peer_slow_events = [b.recv_json() for _ in range(slow_count)]
    check(len(slow_events) == slow_count and slow.sock.fileno() >= 0, "slow client lost events/connection")
    check(len(peer_slow_events) == slow_count, "active peer did not receive slow-client event series")
    results.append(("Slow client", "PASS", "%d events queued in %.2fs; active peer + HTTP responsive" % (slow_count, slow_send_seconds)))
    slow.close()
    time.sleep(2)

    # Exact text limit is accepted; oversized input closes only that client.
    edge, _ = connect_ws()
    edge.send_frame(1, b"x" * 256)
    edge_event = edge.recv_json()
    check(edge_event["revision"] > 1, "256-byte WS frame not processed")
    edge.send_frame(2, b"b" * 8)
    edge.send_frame(1, "after-binary")
    binary_followup = edge.recv_json()
    check(binary_followup["revision"] == edge_event["revision"] + 1, "binary frame disrupted subsequent text frame")
    for _ in range(2):
        a.recv_json()
        b.recv_json()
    results.append(("WS text/binary and 256-byte boundary", "PASS", "256 text accepted; binary frame accepted"))
    edge.close()

    oversized, _ = connect_ws()
    oversized.send_frame(1, b"x" * 257)
    oversized.sock.settimeout(3)
    try:
        data = oversized.sock.recv(16)
        check(data == b"", "257-byte WS frame was not closed: %r" % data)
    except (ConnectionError, OSError, socket.timeout) as error:
        check(not isinstance(error, socket.timeout), "oversized WS frame was not rejected promptly")
    results.append(("WS oversized frame", "PASS", "257-byte frame connection closed"))
    oversized.sock.close()

    results.append(("Bounded POST", "PASS", test_posts()))

    # Close A while B remains, mutate fixture, reconnect A and perform full snapshot resync.
    a.close()
    time.sleep(2)  # allow the server's one-second client sampler to record H5
    b.send_frame(1, "fixture-change-after-disconnect")
    changed_event = b.recv_json()
    a2, reconnect_event = connect_ws()
    full_state, _ = get_snapshot()
    check(full_state["revision"] == changed_event["revision"] == reconnect_event["revision"], "HTTP resync/reconnect state mismatch")
    results.append(("WS reconnect + full HTTP resync", "PASS", "revision=%d in reconnect and full snapshot" % full_state["revision"]))

    a2.close()
    b.close()
    time.sleep(2)

    # Disconnect and reconnect only the ESP32 STA; the host remains online.
    recovery_ws, _ = connect_ws()
    status, _, _ = http_request("POST", NETWORK_LOSS)
    check(status == 202, "ESP-only network-loss trigger returned %d" % status)
    recovery_ws.sock.settimeout(2)
    ws_lost = False
    loss_deadline = time.monotonic() + 15
    while not ws_lost and time.monotonic() < loss_deadline:
        try:
            recovery_ws.recv_frame()
        except (ConnectionError, OSError):
            ws_lost = True
        except socket.timeout:
            pass
    check(ws_lost, "WS connection loss was not observed after ESP STA disconnect")
    recovery_ws.sock.close()
    recovered_state = wait_for_http(45)
    recovery_ws2, recovery_event = connect_ws()
    final_state, _ = get_snapshot()
    check(final_state["revision"] == recovered_state["revision"] == recovery_event["revision"], "post-network-recovery resync mismatch")
    recovery_ws2.close()
    results.append(("Network loss/recovery", "PASS", "ESP STA disconnected/reconnected; HTTP+WS restored without device reset"))

    print("SCENARIO RESULTS BEFORE STABILITY", flush=True)
    for scenario, verdict, evidence in results:
        print("%s | %s | %s" % (scenario, verdict, evidence), flush=True)

    # Short continuous stability run: two persistent sockets, repeated GET/event/POST.
    stable_a, _ = connect_ws()
    stable_b, _ = connect_ws()
    stability_seconds = int(os.environ.get("F83A_STABILITY_SECONDS", "60"))
    started = time.monotonic()
    gets = 0
    posts = 0
    events = 0
    stability_constraint = None
    while time.monotonic() - started < stability_seconds:
        try:
            _, elapsed = get_snapshot()
            check(elapsed < 5000, "HTTP stalled during stability run")
        except Exception as error:
            stability_constraint = "%s after %ds" % (type(error).__name__, int(time.monotonic() - started))
            break
        gets += 1
        if gets % 5 == 0:
            stable_a.send_frame(1, "stability-%d" % gets)
            stable_a.recv_json()
            stable_b.recv_json()
            events += 1
        if gets % 30 == 0:
            try:
                status, _, _ = http_request("POST", BODY, b"periodic")
                check(status == 200, "periodic POST failed: %s" % status)
                posts += 1
            except Exception as error:
                stability_constraint = "%s during periodic POST after %ds" % (type(error).__name__, int(time.monotonic() - started))
                break
        if gets and gets % 30 == 0:
            print("stability progress: seconds=%d GET=%d events=%d POST=%d" % (
                int(time.monotonic() - started), gets, events, posts
            ), flush=True)
        time.sleep(1)
    stability_elapsed = int(time.monotonic() - started)
    if stability_constraint is None and gets < max(3, stability_seconds // 2):
        stability_constraint = "insufficient request cadence (%d GET in %d s)" % (gets, stability_elapsed)
    stable_a.close()
    stable_b.close()
    stability_result = "CONSTRAINT" if stability_constraint else "PASS"
    stability_evidence = "%d s target, ran %d s; %d GET, %d paired events, %d POST" % (
        stability_seconds, stability_elapsed, gets, events, posts
    )
    if stability_constraint:
        stability_evidence += "; " + stability_constraint
    results.append(("Stability", stability_result, stability_evidence))

    print("SCENARIO RESULTS")
    for scenario, verdict, evidence in results:
        print("%s | %s | %s" % (scenario, verdict, evidence))
    timer.cancel()


if __name__ == "__main__":
    main()
