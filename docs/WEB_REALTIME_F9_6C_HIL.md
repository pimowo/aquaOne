# WEB realtime F9.6C — classic ESP32 HIL evidence

Date: 2026-10-05

Repository HEAD: `1a7e496a58b3bde147f1110dad8716089dd25da4`

Scope: uncommitted F9.6B implementation plus isolated HIL fixture and host harness.

## Verdict

- F9.6B implementation: **PASS**.
- F9.6C HIL: **PASS**.
- Backpressure: **SATURATION CONFIRMED** (`RealtimePublicationResult::Busy`).
- Classic ESP32 production transport HIL: **PASS**.
- HIL contained no panic, watchdog, heap corruption or unintended reboot.

## Hardware and effective build environment

- Board: Wemos D1 mini ESP32, classic ESP32 target.
- Chip reported by esptool: `ESP32-D0WD-V3`, revision v3.1.
- Flash: 4 MB, 3.3 V.
- PSRAM: absent (`ESP.getPsramSize() == 0`).
- Serial adapter and port: CH9102, COM6.
- PlatformIO environment/board: `esp32dev` / `esp32dev`.
- Effective platform: pioarduino `platform-espressif32` 53.03.13.
- Arduino source package: Arduino-ESP32 3.1.3.
- Effective precompiled IDF libraries and runtime: IDF release 5.3,
  `v5.3.2-584-g489d7a2b3a-dirty`.
- Current Core baseline: the exact pioarduino release asset
  `https://github.com/pioarduino/platform-espressif32/releases/download/53.03.13/platform-espressif32.zip`.
  It deterministically selects this platform, Arduino-ESP32 3.1.3 and ESP-IDF
  base 5.3.2 for `esp32dev`.
- Historical Phase 8 feasibility evidence remains separately pinned to
  `platformio/espressif32@6.13.0`, Arduino-ESP32 2.0.17 and ESP-IDF 4.4.7.
  It was not the package selected by this HIL.
- IP used by the host harness: `192.168.1.57`.

## Fixture and host harness

The isolated `test_web_realtime_resync_hil` fixture uses the production:

- `EspIdfWebTransport`;
- `RealtimeStreamSequencer` and `RealtimeStreamPosition`;
- `RealtimeSnapshot<HilPayload>` and `PublishedSnapshot`;
- `RealtimeCohortPublisher` with a fixed resource binding;
- StreamStart/client gating, sticky recovery and
  `serviceRealtimeRecovery()`.

The authoritative test payload contains revision, value, bitwise inverse and a
checksum. The HTTP reader rejects unavailable wrappers and the Python harness
checks inverse/checksum consistency. Test-only bodyless endpoints request
Application-loop operations; they are not proposed production APIs.

The host harness uses bounded HTTP requests and a small raw RFC 6455 client. It
actively fails on notification-before-marker, runtime mismatch, sequence gap,
mixed payload, snapshot older than StreamStart, premature recovery clear and
stale notification across recovery.

Credentials came from a local ignored `secrets.h`. Values were not printed,
stored in this report or added to Git. PlatformIO artifacts remain under the
ignored `.pio` directory.

## Functional results

### Baseline and continuity

- Fresh baseline snapshot and first WS StreamStart used the same runtime and
  `BeforeFirst`; the first semantic change was sequence 1.
- One-client path delivered sequences 1 through 5 contiguously.
- Overlap run captured snapshot position 5 and eight notifications; all eight
  positions 6 through 13 were applied after discarding positions `<= 5`.
- Reconnect produced StreamStart 13 and a full HTTP snapshot at or beyond it;
  no replay log was used.

### Two clients and connection lifecycle

- Two clients connected at position 13 and both received contiguous changes.
- Client A was disconnected; client B continued and received sequence 17.
- Fifteen additional connect/close/reconnect cycles all received StreamStart
  17, exceeding the seven-slot table capacity without slot exhaustion.
- Internal fd values are not exposed by the production API. Repeated reconnect
  therefore proves generation behaviour indirectly: no stale marker, stale
  LIVE transition or notification-before-marker was observed.
- The forced CONNECTING race resolved as `StreamStart` followed by the newer
  notification. The client did not become LIVE with a silent gap.

### Controlled failure and no-next-event recovery

- Snapshot-build failure spent sequence 19, withheld WS notification, made the
  snapshot unavailable, set sticky recovery and closed the old connection.
- Explicit `republishCurrentCohort()` rebuilt and cleared recovery at the same
  sequence 19. A new client received StreamStart 19.
- Deterministic notification pre-acceptance failure spent sequence 20 while
  leaving coherent HTTP cohort 20 available. Recovery cleared at sequence 20
  without waiting for sequence 21.

### Real saturation and stale-work protection

- The HTTPD task was intentionally held by a test-only endpoint while the
  Application loop submitted a bounded burst through the production transport.
- Four notification work items were accepted. The **fifth publication returned
  `Busy`**, establishing the real four-slot boundary.
- Sticky recovery suppressed pending old-generation fanout, clients were
  closed, cohort 25 was republished, recovery cleared and a new connection
  received StreamStart 25.
- The first frame of the new connection was always StreamStart; no queued
  old-generation notification appeared after clear.

### Slow client and HTTP progress

- One WS client deliberately did not read and used a small receive buffer.
- A second client read normally and received 273 contiguous notifications
  before the measured backpressure/recovery boundary.
- The fixture used 256-byte notification payloads and a paced bounded load.
- HTTP remained responsive. Snapshot request latency during this run was
  62.6–123.7 ms. A separate status request observed a worst delay of 1194.1 ms,
  showing measurable HTTPD interference from the slow-client/load case.
- Backpressure entered sticky recovery; no panic, watchdog or reboot occurred.
- No slow-client timeout threshold is accepted from this measurement.

### Close supervisor

- The fixture scheduled one bounded service call every 20 ms.
- Final counters: 36 `Progress`, 96 `RetryNeeded`, zero
  `TransportRecycleSuggested`, four recovery requests and four successful
  clears.
- A successful close request left the client CLOSING until fd/session became
  inactive. `closeRequested` prevented immediate duplicate close queueing.
- Close-work queue failure during saturation was **not observed**; later
  progress after pending close was observed.

### Runtime restart and network

- Intentional restart changed RuntimeIdentity from `e3a7ac42d5bc862f` to
  `163d7f780cb22a39`.
- After restart both HTTP snapshot and StreamStart returned to `BeforeFirst`
  under the new runtime. Old runtime/sequence state was not reused.
- A dedicated ESP-only network loss/restore scenario was not run. During one
  reflashing iteration the AP temporarily refused association; bounded fixture
  retry restored connectivity. This was outside the final functional run.
- Heartbeat behaviour against a silent network blackhole was not exercised;
  heartbeat policy remains open.

## HTTP latency summary

Values are host-observed request-to-complete-response measurements, not a
platform benchmark.

| Scenario | Count | Minimum | Median | Maximum |
| --- | ---: | ---: | ---: | ---: |
| HTTP only | 2 | 66.7 ms | 310.1 ms | 310.1 ms |
| One WS / stability | 84 | 58.3 ms | 66.5 ms | 614.4 ms |
| Two WS | 1 | 139.9 ms | 139.9 ms | 139.9 ms |
| Slow client snapshot | 7 | 62.6 ms | 113.5 ms | 123.7 ms |
| Recovery | 12 | 60.6 ms | 75.6 ms | 317.9 ms |

## Internal heap samples

All figures are bytes from `MALLOC_CAP_INTERNAL`. Largest block is the largest
internal-capable block; PSRAM was absent.

| Point | Free | Minimum seen | Largest block |
| --- | ---: | ---: | ---: |
| H1 server started | 228660 | 225012 | 110580 |
| H2 one WS | 226624 | 222932 | 110580 |
| H3 two WS | 225676 | 217580 | 110580 |
| H5 saturation recovered | 225252 | 215052 | 110580 |
| H6 clients closed | 226380 | 203008 | 110580 |
| H7 final stability | 226356 | 203008 | 110580 |

No H0 pre-server heap sample was recorded. H1 is the earliest comparable
sample. Free internal heap after the final stability run was within 2304 bytes
of H1, while the historical minimum captured transient load.

## Stability run

- Duration: 60.17 s.
- HTTP requests: 162.
- Notifications checked: 81.
- Reconnects: 4.
- Final pre-restart position/revision: 384/384.
- Final counters: 380 accepted publications, three Busy outcomes (controlled
  notification failure, real saturation and slow-client boundary), zero
  `QueueWorkFailure`.
- Panic/watchdog/heap corruption/unexpected reboot: none observed.

## Limitations and decisions deferred

- Exact internal fd reuse was not externally visible; stale-work behaviour was
  verified through frame ordering and repeated reconnects.
- Network loss/restore and silent blackhole detection were not run.
- Close queue submission failure was not reached.
- The four work slots, 256-byte capacity, seven clients, latency values and any
  slow-client timeout remain implementation measurements, not platform policy.
- The test confirms the effective IDF 5.3.2 runtime of the Current Core
  baseline. Phase 8's IDF 4.4.7 evidence is historical only.

## Recommendation

The classic ESP32 residual in WEB-103 can be closed for the pinned Current
`esp32dev` baseline and this F9.6B transport implementation. Deployment still
needs the normal product composition work. Product projects remain outside this
Core pin until TOOLCHAIN-2 aligns their own toolchains.
