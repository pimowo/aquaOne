# F8.3C — Web transport comparison and WEB-103 decision

**Decision: WEB-103 ACCEPTED — TARGET: ESP-IDF `esp_http_server` used from
Arduino-ESP32 for the v1 Web transport.** The decision applies to the tested
ESP32-S3 and Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7 baseline. It selects a Phase 9
implementation target; production Web has not been migrated. F8.3A concluded
**IDF HIL PASS**. F8.3B concluded **ASYNC HIL INCONCLUSIVE**, not Async PASS or
FAIL. The existing Arduino `WebServer` remains a CURRENT legacy HTTP backend
but is **NOT FEASIBLE WITH CURRENT STACK** for WEB-001 HTTP+WS on one listener.

## Evidence and requirements

F8.1 audited the CURRENT `WebServer`. F8.2 compiled isolated candidates using
the `esp32dev` AP profile. F8.3A and F8.3B ran separate STA HIL on one physical
ESP32-S3 with 4 MB flash and 2 MB PSRAM. Their fixture uses one port 80 server,
a full HTTP snapshot and WS change notifications. The tested stack pins
Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7, ESPAsyncWebServer 3.6.0 and AsyncTCP
3.3.2. No new HIL or native regression was run for this docs-only comparison.
F8.2 compile evidence covers its `esp32dev` profile; the runtime verdicts cover
the tested ESP32-S3 only. Classic ESP32 behavior still needs its own hardware
validation before a production rollout to that board.

| Criterion | IDF `esp_http_server` | ESPAsyncWebServer + AsyncTCP | Decision relevance |
| --- | --- | --- | --- |
| Compile on current Arduino platform | F8.2 compile/link PASS | F8.2 compile/link PASS | Both usable with the pinned baseline. |
| One server/listener and port | One `httpd_handle_t` on port 80 | One `AsyncWebServer(80)` with WS handler | Both meet the physical WEB-001 boundary. |
| HTTP with 1 persistent WS | F8.3A PASS; 20 GET | F8.3B PASS; 20 GET | Both meet the tested concurrency requirement. |
| HTTP with 2 persistent WS | F8.3A PASS; 20 GET | F8.3B PASS; 20 GET | Both meet the tested two-client requirement. |
| Reconnect and full HTTP resync | PASS | PASS | HTTP snapshot can restore full fixture state after WS reconnect. |
| Network loss/recovery | ESP-only Wi-Fi recovery PASS; HTTP/WS usable afterward | ESP-only Wi-Fi recovery PASS; `server_begin_count=1` | Both recovered in the tested scenario without restarting the transport server. |
| Slow reader | 32 events later delivered in the tested case; queue occupation not measured | 32/32 later delivered; sampled queue maximum 2 and no full sample | Neither test establishes saturation or a backpressure policy. |
| Lifecycle confidence | HIL PASS: clean close, recovery and 61 s run without observed panic/reboot | One real heap panic in teardown; legal targeted retests did not reproduce it | IDF has stronger confirmed evidence for this baseline; Async root cause remains unknown. |
| Static footprint | F8.2 RAM 43,416 B; linked flash 759,525 B; bin 766,096 B | RAM 43,900 B; linked flash 801,541 B; bin 808,112 B | IDF spike measured smaller; these are whole-spike builds, not library-only costs. |
| Runtime and task memory | H0–H6 heap samples with documented boot provenance; HTTPD server task stack 4,096 B | H0–H5 in the panic run, H6 after reboot; AsyncTCP task configured for 16 KiB stack | Different boot histories and allocation regions preclude a single comparable “RAM cost”; task high-water was not measured. |
| Bounded body handling | Checks `Content-Length` before `httpd_req_recv`; caller-owned 256 B buffer; 0/1/256/257/1024 B PASS | Raw body chunk callbacks and bounded fixture storage; same boundary cases PASS, after library pipeline received chunks | IDF provides the more direct pre-receive bound for future Config/Restore handlers; final policy remains Phase 9 work. |
| WS/frame model | Low-level frame API and caller-owned RX buffer; sync and async send; more manual lifecycle | Higher-level broadcast, fragmentation metadata and built-in client queues/cleanup | Async is more convenient; IDF gives more explicit buffer control. Both passed tested frame boundaries. |
| Callback context | HTTPD server task | `async_tcp` task, core 1 in the tested build | Both require serialization into the Application boundary. |
| Auth capability | Request/handshake headers available before response | Request/handshake headers and middleware available | Both can support later Auth; neither HIL implemented it. |
| Transport dependencies | Framework-native component | Two pinned external libraries | Shared Core maintenance favors the framework-native candidate, without treating external libraries as defects. |
| Final candidate HIL verdict | **IDF HIL PASS** | **ASYNC HIL INCONCLUSIVE** | Select IDF for v1 TARGET on the tested platform/version set. |

The existing Arduino `WebServer` is not an active candidate: F8.1 found no WS
upgrade/frame API and one active-client handling. A separate WS listener would
violate WEB-001. It may remain the legacy CURRENT HTTP backend until Phase 9.

The IDF 61-second window completed 39 GET, 7 event pairs and 1 POST without
observed restart. The earlier Async 60-second window completed 60 GET,
12 event pairs and 2 POST; the corrected review window completed 48 GET,
9 event pairs and 1 POST. Neither successful window erases the one observed
Async teardown panic or proves long-term lifecycle safety.

## Footprint and runtime limits

F8.2 measured the same `esp32dev` AP build setup for these compile-only
figures; F8.3 HIL used ESP32-S3 STA firmware. The baseline lacks the candidate
features, and the IDF F8.2 spike includes an extra bounded POST. These numbers
show the size of each concrete F8.2 spike, not pure transport library overhead
or the final S3 production firmware footprint.

| F8.2 build | Linked RAM | Linked flash | `firmware.bin` | Delta vs baseline (RAM / flash / bin) |
| --- | ---: | ---: | ---: | --- |
| AP baseline | 43,408 B | 725,721 B | 732,304 B | — |
| IDF | 43,416 B | 759,525 B | 766,096 B | +8 / +33,804 / +33,792 B |
| Async | 43,900 B | 801,541 B | 808,112 B | +492 / +75,820 / +75,808 B |

Both HIL reports recorded runtime heap. IDF's H0/H1 came from a deliberate
control reset, while later markers came from a successful scenario run. Async's
H0–H5 came from the boot that later panicked; H6 came from a separate recovery
boot. In the successful workload windows neither report observed a growing
internal free-heap loss, but neither measured a long soak or task stack
high-water. The Async H4 `min free` value (266,164 B) exceeds its reported
earlier H2/H3 minimum (261,492 B), so that marker series does not support a
precise longitudinal trend by itself. The Async panic prevents a memory-safety
PASS. Both fixtures used
`ESP.getFreeHeap()` / `ESP.getMinFreeHeap()` for internal heap; their largest
8-bit-capable block may include PSRAM and is not directly comparable with
internal free heap. No single runtime-memory winner follows from these samples.

Each latency observation covers request start through complete HTTP response
receipt, including TCP setup, with no intentional sleep inside the measured
interval. Conditions, harness behavior, Wi-Fi timing and task models can affect
the result. These single-device HIL observations are not a benchmark or a basis
to claim that Async is generally faster.

| HTTP GET mode | IDF min / avg / max | IDF samples | Async min / avg / max | Async samples |
| --- | --- | ---: | --- | ---: |
| HTTP only | 302.32 / 346.25 / 539.36 ms | 25 | 13.04 / 29.77 / 251.19 ms | 20 |
| HTTP + 1 WS | 296.64 / 307.28 / 319.74 ms | 20 | 9.18 / 15.36 / 41.64 ms | 20 |
| HTTP + 2 WS | 295.37 / 306.81 / 315.75 ms | 20 | 8.47 / 13.06 / 32.62 ms | 20 |

## Decision scope and next step

IDF is selected because the tested same-port HTTP+WS, two-client concurrency,
reconnect/full resync and network recovery have HIL PASS; its clean-close and
short stability evidence has no observed panic; the measured F8.2 spike is
smaller and adds no external transport library. Async satisfies the tested
functional model and offers a higher-level WS API, but its lifecycle evidence
is inconclusive after **ONE OBSERVED PANIC — NOT REPRODUCED**. This is not a
confirmed AsyncTCP or lwIP defect. A later platform/library upgrade or new
isolated lifecycle evidence may justify reevaluation.

WEB-001 is proven for the IDF fixture: one physical listener and port carry
HTTP and persistent WS concurrently. Phase 9 must add the transport-task to
serialized-Application boundary, bounded payload and queue policy, and
drop/disconnect behavior for slow clients. The useful source-of-truth model is
full HTTP snapshot plus WS notification and full HTTP resync after reconnect;
the final wire format, replay/history and endpoint schema are not chosen here.
WEB-101 public/auth policy, WEB-102 HTTP API/schema, RT-101 realtime protocol
and backpressure, and SEC-101 Auth remain open. TLS was not tested and is
outside WEB-103. Header access supports future Auth design but does not prove
its implementation. No direct handler-to-Domain or hardware call is approved.

Phase 8 remains active until a separate F8.4 final audit/closure. ROADMAP is
unchanged. F8.1–F8.3B reports and both isolated spikes remain as evidence.
The previous F8.2 native baseline was 368/368 PASS; this decision changes
documentation only.
