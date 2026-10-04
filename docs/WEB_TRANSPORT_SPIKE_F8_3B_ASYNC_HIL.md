# F8.3B — Async Web transport HIL

**Result: ASYNC HIL INCONCLUSIVE after teardown review.** This report covers only the isolated
`ESPAsyncWebServer` candidate. WEB-103 remains **DECISION REQUIRED**; this HIL
does not select the Phase 9 backend or change production Web.

## Test setup and provenance

- Board on COM14: ESP32-S3 QFN56 rev 0.2, 4 MB XMC flash, 2 MB AP_3v3 PSRAM.
  Runtime reported 4,194,304 flash bytes and 2,095,103 PSRAM bytes. The
  dedicated S3 profile sets a 4 MB image, QSPI PSRAM and USB CDC. PlatformIO's
  informational board label still says N8/no PSRAM, while esptool and runtime
  identify the actual 4 MB + 2 MB device.
- `ESP32Async/ESPAsyncWebServer 3.6.0`, `ESP32Async/AsyncTCP 3.3.2`,
  Arduino-ESP32 2.0.17 and its ESP-IDF 4.4.7 base. The dedicated
  `web_spike_async_hil` build and upload
  succeeded.
- ESP ran as STA on the same IoT WLAN as F8.3A and received DHCP
  `192.168.1.137`. The PC stayed on its normal WLAN; association and TCP/443
  connectivity were checked before HIL. No host network settings changed.
- Credentials came from a local ignored header. That header and generated
  firmware image were removed after testing. No credential is present in
  tracked files or commit; this work is uncommitted. The HIL image remains
  programmed on the device and contains the local STA credentials until a
  later firmware upload replaces it.
- F8.2's 368/368 native-test evidence is preserved and was not rerun.

## Scenario results

| Scenario | Result | Evidence |
| --- | --- | --- |
| HTTP only | PASS | Snapshot returned HTTP 200 with full fixture state; 25 requests including 20 concurrent GETs passed. |
| One WS | PASS | HTTP Upgrade 101 and greeting received; 20 snapshot GETs succeeded while A stayed open. |
| HTTP + one WS | PASS | All 20 GETs succeeded; WS remained usable. |
| Two WS | PASS | A and B connected and both received the same broadcast revision. |
| HTTP + two WS | PASS | All 20 GETs succeeded with both WS connected; both received subsequent events. |
| Directed response + broadcast | PASS | A received directed acknowledgement and broadcast; B received the same broadcast revision. The Async API model is not labelled sync/async by analogy with IDF. |
| Slow client and queue | PASS for this fixture load | A sent 32 events while a third client did not read. That client drained 32/32 afterward; B and HTTP stayed responsive. Serial `textAll` status was `ENQUEUED`; sampled per-client queue maximum was 2, `queueIsFull=false`, zero full samples. This is not saturation/backpressure testing. |
| POST body limits | PASS | 0 B → 400; 1 B → 200; 256 B → 200; 257 B → 413; 1024 B → 413. Oversized chunks were not copied into the fixture buffer. The response occurs after the Async request pipeline has received body data; internal/network buffering was not measured or prevented. |
| WS text/binary limits | PASS | Small text/binary accepted; 256-byte text accepted; 257-byte text closed with fixture code 1009. The 256-byte limit is fixture policy. |
| WS fragmented text | PASS | Two client frames (text plus continuation) produced one acknowledgement and one broadcast revision. The fixture accounts for `num`, `index`, `len`, `final`, and message opcode. |
| Ping/pong | PASS | Raw client received pong with the same payload; firmware logged incoming ping. |
| Clean close | PASS in review reproduction; original failure unresolved | Individual clean close reply and disconnect callback were observed. The original extended run ended in heap corruption before its second close completed; corrected review runs did not reproduce it. |
| ESP-only network loss/recovery | PASS | HIL endpoint invoked `WiFi.disconnect()` on ESP. Both WS dropped; STA rejoined and HTTP/WS plus full snapshot resync worked. Serial showed disconnect reason 8 and `server_begin_count=1` after recovery. PC WLAN stayed connected. |
| 60-second stability | PASS for the isolated soak | 60.0 s with two WS, 60 GETs, 12 paired events and 2 bounded POSTs. No panic, watchdog or reboot during the soak. ESP-side disconnect then closed both clients and recovery succeeded. |

The HTTP snapshot (`revision`, `value`) is authoritative full fixture state; WS
is notification. Reconnect validation compared the new greeting and full HTTP
snapshot at the same revision. No replay/history was added.

## Runtime evidence

### Heap markers

H0–H5 below came from one main functional HIL boot, which included concurrent
GET and ran through the reconnect scenario before the teardown panic. H6 is
from a later post-panic recovery boot, where ESP-side STA disconnect closed all
clients. Provenance is kept explicit rather than treating this as one
continuous H0–H6 run. `Free` is `ESP.getFreeHeap()` and `Min free` is
`ESP.getMinFreeHeap()`, both using `MALLOC_CAP_INTERNAL` in Arduino-ESP32
2.0.17. `Largest` is `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`;
that capability set includes PSRAM. The columns are not directly comparable,
and these are runtime heap bytes rather than static linker RAM.

| Marker | Free | Min free | Largest | Provenance |
| --- | ---: | ---: | ---: | --- |
| H0 — before transport/network | 335,248 | 330,016 | 2,064,372 | Main HIL boot |
| H1 — Async server, 0 WS | 279,172 | 278,948 | 2,064,372 | Main HIL boot |
| H2 — 1 WS | 274,388 | 261,492 | 2,064,372 | Main HIL boot |
| H3 — 2 WS | 272,952 | 261,492 | 2,064,372 | Main HIL boot |
| H4 — during concurrent GET | 266,204 | 266,164 | 2,031,604 | Main HIL boot; after total GET 10, the fifth request in the 20-request concurrent burst |
| H5 — one WS closed | 273,268 | 261,492 | 2,064,372 | Main HIL boot |
| H6 — all WS closed | 278,636 | 265,744 | 2,064,372 | Post-panic recovery boot; ESP-side STA disconnect |

In separate runs, free heap returned to about 278.5 KB after WS cleanup. The
60-second soak did not show progressive decrease; it cannot establish
long-term leak freedom. AsyncTCP's library task configuration indicates a
16 KiB stack; stack high-water was not measured.

### Latency

Each value is 20 sequential GETs measured from immediately before
`HTTPConnection.request()` through receipt of the complete response body. It
includes TCP setup/request/response, and excludes sleeps, WS waits, setup and
reconnect. The separate 20-concurrent-GET pass succeeded, but its individual
latency samples were not retained.

| Condition | Min | Average | Max | Samples |
| --- | ---: | ---: | ---: | ---: |
| HTTP only | 13.04 ms | 29.77 ms | 251.19 ms | 20 |
| HTTP + 1 WS | 9.18 ms | 15.36 ms | 41.64 ms | 20 |
| HTTP + 2 WS | 8.47 ms | 13.06 ms | 32.62 ms | 20 |

These are observations from one device and WLAN, not a benchmark or direct
ranking against F8.3A.

### Callback and failure evidence

Serial identified HTTP and WS callbacks in task `async_tcp`, core 1, outside
the Arduino application loop. This fixture calls no Domain or hardware code.
For complete single-chunk WS frames, version 3.6.0 reported
`message_opcode=0` while `opcode=1` for text (and `opcode=2` for binary). The
fixture used the first-frame `opcode` fallback and retained it for
continuations.

The extended functional run ended with actual heap-poisoning failure during
TCP teardown:

- `CORRUPT HEAP: Bad head ... Expected 0xabba1234 ...`, then assertion in
  `multi_heap_free` and device reboot (`RTC_SW_CPU_RST`);
- decoded stack was in lwIP `mem_free` / `memp_free` / `tcp_close`, called from
  AsyncTCP 3.3.2 `_tcp_close_api` on `tcpip_thread`; the decode used the ELF
  matching the uploaded image at the time of the panic (the ELF was removed
  afterward, and the later review build was not used to decode this stack);
- the host timed out waiting for the second close reply. This was not a
  watchdog or deliberate upload/DTR reset.

The trigger was observed once in the extended close sequence. During review,
the Python harness was found to close raw TCP after receiving the server's
1009 CLOSE without first sending a WebSocket CLOSE response. The harness now
attempts to echo a masked CLOSE before releasing TCP (unless the server has
already closed the connection) and requires the slow reader to receive all
32 events. The server fixture does not manually delete WS clients; its tracked
client pointer is cleared in the disconnect callback. It calls the library's
public `cleanupClients()` from `loop()`, not from a WS callback. A teardown
race inside or around the library has not been ruled out.
The original panic remains real, but its root cause and whether it occurs with
a legal client close sequence are unproven. The observed stack is a teardown
path while using ESPAsyncWebServer/AsyncTCP, not proof of an lwIP or AsyncTCP
defect.

On separate fresh boots after the harness correction, 20 sequential A connect
and clean-close cycles passed, then 20 A+B connect / small event / clean-close
cycles passed. A corrected functional sequence including the 257-byte close
handling, ESP-only recovery and final two-client teardown also passed without
panic. Its 60-second workload completed 48 GET, 9 paired events and 1 POST;
these are review-run counts, distinct from the earlier 60 GET / 12 events /
2 POST soak. **ONE OBSERVED PANIC — NOT REPRODUCED.** This does not establish
long-term memory safety or a final Async candidate PASS.

## Measurement limits and decision status

**Measured:** build/upload, S3 flash/PSRAM, concurrent HTTP, one/two WS with
HTTP, directed/broadcast delivery, frame boundaries and fragmentation, POST
boundaries, queue observations for the tested slow reader, ESP-side recovery,
runtime heap markers, latency samples and a 60-second soak.

**Observed:** zero `message_opcode` on complete frames; `ENQUEUED` sends and
maximum sampled queue length 2 in the 32-event test; recovery without a second
server `begin()`; one lwIP heap corruption panic during the original extended
teardown, before the client CLOSE handshake was corrected.

**Not measured:** AsyncTCP task stack high-water, saturated queue/long
backpressure, Async internal POST buffering, long soak, TLS/auth, or production
protocol and integration behavior.

**Constraint:** ordinary soak and ESP-side reconnect passed. The original
extended teardown corrupted heap and rebooted the device, but legal teardown
review runs did not reproduce it after the harness correction. Overall
candidate verdict remains **INCONCLUSIVE** pending a clean lifecycle review;
the earlier **ASYNC HIL FAIL** is not confirmed. WEB-103 remains **DECISION
REQUIRED**; no backend is selected here.

No production code, F8.3A evidence, or reference files were changed. Native
tests were not rerun; F8.2's 368/368 PASS evidence remains unchanged.
