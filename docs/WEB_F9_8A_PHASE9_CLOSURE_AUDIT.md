# F9.8A — Phase 9 production Web and Realtime closure audit

**Status (2026-10-09): A — PHASE 9 CLOSABLE NOW; READY FOR F9.8B CLOSURE.** This is a documentation-only decision. Phase 9 remains **IN PROGRESS** until the separate final closure checkpoint. Phase 10 is **NOT STARTED**.

## Baseline and interpretation

Audited committed `main == origin/main` at `1302d7094c9e0ba43c093f2c5bfb287a040ca889` (`Migrate Doser to native web`). Tracked tree and index were clean. The only unrelated untracked paths were `aquaOneCore/test/test_web_realtime_resync_hil/hil_results.json` and `reference/`; neither is audit evidence or part of this checkpoint. No build, flash, HIL or source/test change belongs to F9.8A.

The accepted [F9.1 sequence](WEB_REALTIME_F9_1_DESIGN.md) assigns the native HTTPD owner to F9.2, typed reads to F9.3, bounded actions to F9.4, WS capability to F9.5, coherent recovery and measured backpressure to F9.6, product Web route migration to F9.7, and an integration/closure audit to F9.8. Its production target requires one transport capable of HTTP and WS on one port; it does not explicitly require a product WS subscription in F9.7. [F9.7A §7](WEB_F9_7A_MIGRATION_AUDIT.md) explicitly makes Luma WS optional pending measured value and allows no product WS endpoint where there is no approved subscriber/resource need. The [HA standard §6](HOME_ASSISTANT_INTEGRATION_STANDARD.md) explicitly permits a CURRENT polling-only product until its Realtime cutover. These accepted boundaries make production product WS **nonblocking for Phase 9**, while prohibiting any claim that product Realtime is already CURRENT. The Core HIL fixture is not a product deployment.

## F9.1–F9.7 completion map

| Gate | Status at this baseline | Checkpoint and evidence |
| --- | --- | --- |
| F9.1 | CLOSED design; its initial CURRENT inventory is historical | `49bdd04` `Define Web realtime architecture`; [F9.1 design](WEB_REALTIME_F9_1_DESIGN.md) |
| F9.2 | CLOSED/CURRENT native HTTPD transport foundation | `a277215` `Add IDF web transport foundation`; [Core transport](../aquaOneCore/include/AquaCore/Web/EspIdfWebTransport.h) |
| F9.3 | CLOSED/CURRENT typed snapshot publication | `089cad3` `Add web snapshot publication foundation`; [PublishedSnapshot](../aquaOneCore/include/AquaCore/Web/PublishedSnapshot.h) |
| F9.4 | CLOSED/CURRENT bounded action bridge | `397655f` `Add web action bridge foundation`; [WebActionBridge](../aquaOneCore/include/AquaCore/Web/WebActionBridge.h) |
| F9.5 | CLOSED/CURRENT WS publication capability | `265e2f6` `Add realtime websocket foundation`; [Realtime types](../aquaOneCore/include/AquaCore/Web/Realtime.h) |
| F9.6A | CLOSED design; historical design-only status | `1a7e496` `Define realtime resync architecture`; [F9.6A contract](WEB_REALTIME_F9_6A_RESYNC_DESIGN.md) |
| F9.6B/C/D | CLOSED/CURRENT Core resync foundation and physical HIL | `a0b6c3f` `Add realtime resync and backpressure recovery` includes implementation, native tests, HIL fixture and [F9.6C report](WEB_REALTIME_F9_6C_HIL.md). Its `READY TO CHECKPOINT` wording in older architecture text is historical: the commit is present in this HEAD. |
| F9.7 | CLOSED production HTTP migrations for Luma, Hydro and Doser | `15d9f5b` Luma, `c09b47f` Hydro, `1302d70` Doser; [Luma HIL](WEB_F9_7D2_LUMA_HIL.md), [Hydro gate/current HIL status](WEB_F9_7E1_HYDRO_MIGRATION_GATE.md), [Doser OTA/HIL result](WEB_F9_7G1_DOSER_OTA_DESIGN.md) |

F9.7B/C and F9.7F/G supporting checkpoints are visible between these commits. F9.7A describes its own earlier baseline and is retained as historical audit, not as a present-day assertion that Doser still uses legacy Web. F9.7H in that document is a recommended future cross-product/toolchain split for Gas/Fauna/Clima when buildable; it was not accepted as a Phase-9 exit criterion for the three existing production Web products.

## Core Realtime CURRENT and its limits

`EspIdfWebTransport` owns one HTTPD handle and can register one optional WS endpoint on the same listener as bounded HTTP routes. `RealtimeStreamSequencer` uses an Application-owned, contiguous per-runtime sequence and `RuntimeIdentity`; `RealtimeStreamPosition` represents `BeforeFirst` separately from a numbered position. `RealtimeSnapshot<T>` binds a typed payload to runtime identity, watermark and coherence. `RealtimeCohortPublisher` publishes/restamps an entire explicitly bound cohort before normal notification, and can republish the current cohort without issuing another position. The transport sends `StreamStart` before a client becomes LIVE, uses a fixed client table (7) and notification work pool (4 × 256 B), and has sticky resync, generation-gated old work, bounded close/retry and lifecycle recovery. These capacities are implementation measurements, not accepted protocol limits. See [RealtimeResync.h](../aquaOneCore/include/AquaCore/Web/RealtimeResync.h), [transport API](../aquaOneCore/include/AquaCore/Web/EspIdfWebTransport.h) and [F9.6A contract](WEB_REALTIME_F9_6A_RESYNC_DESIGN.md).

The [F9.6C classic ESP32 HIL](WEB_REALTIME_F9_6C_HIL.md) used the production transport and resync classes in an isolated fixture: 402/402 native tests and `esp32dev` compile/link passed; physical HTTP+WS, `StreamStart`, coherent HTTP watermark, one/two clients, reconnect, runtime change, gap recovery, slow client and real four-slot saturation passed. The fifth queued notification returned `Busy`. A 60.17 s run completed 162 HTTP requests and 81 checked notifications without panic, watchdog or unintended reboot. HIL did not establish an external WS schema, a product WS endpoint, silent-blackhole heartbeat policy, every control-queue failure path or a universal board/latency limit.

The current `StreamStart` encoder is explicitly marked **provisional internal F9.6B**, not final RT-101 wire, in [EspIdfWebTransport.cpp](../aquaOneCore/src/Web/EspIdfWebTransport.cpp). Its tested byte layout must not be advertised as a stable public protocol. The logical `Notification(runtime, sequence, kind, payload)` and `StreamStart(runtime, position)` relation is accepted; final encoding, names and payload schema remain open.

## Product composition and need

| Product | Native HTTP production | WS endpoint | `RealtimeSnapshot` cohort / `StreamStart` | Product notifications | Need now |
| --- | --- | --- | --- | --- | --- |
| Luma | YES: one native listener, typed HTTP snapshots and bounded actions | NO | NO / NO | NO | OPTIONAL for Phase 9; strongest later reference candidate because mode/profile/manual and status change, with existing classic ESP32 HIL and small cohort. Polling UI remains CURRENT. |
| Hydro | YES: one native listener, typed status/settings/diagnostics reads and bounded actions | NO | NO / NO | NO | OPTIONAL for Phase 9; tank/top-up/alarm/buzzer/network changes have future live value, but physical hardware HIL and more safety/secret-sensitive projections remain. Polling remains CURRENT. |
| Doser | YES: one native listener and native streaming OTA | NO | NO / NO | NO | NO CURRENT NEED for a product stream: no product status API/WS UI; restart or OTA progress is not automatically a notification. MQTT/Discovery remains LEGACY CURRENT. |

Evidence: production [Luma composition](../aquaOneLuma/src/main.cpp), [Hydro composition](../aquaOneHydro/src/main.cpp), [Doser composition](../aquaOneDoser/src/main.cpp), [project matrix](PROJECT_MATRIX.md) and [F9.7A product analysis](WEB_F9_7A_MIGRATION_AUDIT.md). Each production product uses `PublishedSnapshot<T>` without the `RealtimeSnapshot<T>` wrapper, per-product `RuntimeIdentity`, shared stream watermark or coherent full-resync cohort. Its current HTTP response therefore cannot complete the accepted F9.6 WS-first resync handshake. This is a **product Realtime cutover gap**, explicitly deferred by the optional F9.7A composition policy, not proof of product Realtime or a hidden Phase-10 client implementation. Before any product WS claim or HA WS client use, a separate product server cutover must provide those metadata, cohort, notifications, recovery and a reviewed external wire mapping.

Luma is the smallest sensible *future* reference if a client need is approved: it already has bounded typed status plus Core projections, a native action path, a classic ESP32 build and physical Web HIL. Hydro adds actuator/config and physical HIL limits; Doser has no current status stream and OTA/restart are system workflows. This comparison does not impose a new F9.8B implementation gate.

Luma/Hydro/Doser and Core pin pioarduino 53.03.13 with Arduino-ESP32 3.1.3 / IDF 5.3.2 for their respective classic ESP32 or S3 boards. Their production builds and board-specific Web/OTA HIL prove those *product profiles*, not WS composition on those products or every future board. F9.7A's unpinned product-toolchain warning describes its older baseline; the generic example in `aquaOneCore/README.md` still needs a separate documentation correction.

## Evidence matrix

`PASS` names an actual test/compile/HIL result; `—` means that capability is not composed there. F9.8A did not rerun these gates.

| Capability | Native tests | ESP32 compile/link | Physical HIL | Production composition |
| --- | --- | --- | --- | --- |
| Core HTTP/one owner | F9.2 and F9.7 native transport/integration tests PASS | Core classic and three product builds PASS | F8.3A/F9.6C Core; Luma/Hydro/Doser Web PASS | YES, all three products |
| Core typed snapshots | F9.3/native integration tests PASS | Core and products PASS | F9.6C fixture; product GETs PASS | YES, all three products |
| Core bounded actions | F9.4/F9.7 tests PASS | Core and products PASS | Luma/Hydro actions and Doser OTA PASS as tested; Doser standalone restart route NOT TESTED on hardware | YES, product-specific routes |
| Core Realtime WS | F9.5/F9.6 402/402 Core suite PASS | `esp32dev` PASS | F9.6C classic ESP32 PASS | Core capability CURRENT; no product endpoint |
| Realtime coherent resync | F9.6 402/402 Core suite PASS | `esp32dev` PASS | F9.6C classic ESP32 PASS | Core capability CURRENT; no product cohort |
| Luma Web | Native/semantic tests PASS | Production classic ESP32 PASS | F9.7D2 physical HIL PASS | Native HTTP CURRENT, polling-only |
| Hydro Web | Native/semantic tests PASS | Production S3 PASS | F9.7E3 bare-board HIL PASS with hardware limits | Native HTTP CURRENT, polling-only |
| Doser Web | Native/semantic cutover tests PASS | Production S3 PASS | F9.7G5 bare-board HIL PASS | Native HTTP CURRENT, no product WS |
| Doser streaming OTA | Multipart/bridge/cutover tests PASS | Production S3 PASS | F9.7G5 real OTA, slot/hash/NVS/soak PASS | Product-local native OTA CURRENT |

## Open decisions and residuals

| Item | Phase-9 classification | Boundary |
| --- | --- | --- |
| RT-101 final wire envelope, WS frame spelling and payload schemas | OPEN NONBLOCKING | F9.6 accepted only logical continuity; resolve before an external WS contract/product cutover. No HA entity schema is set here. |
| RT-101 heartbeat / silent-blackhole detection | OPEN NONBLOCKING | HIL did not cover a silent blackhole; decide with later measurements. |
| RT-101 numeric capacity, slow-client thresholds and full backpressure policy | OPEN NONBLOCKING | Bounded implementation and measured saturation exist; no universal thresholds accepted. |
| RT-101 Auth/session, SEC-101 and WEB-101 | OPEN NONBLOCKING | Security/public endpoint policy remains OPEN. F9.8A does not endorse unauthenticated product WS; Doser Basic Auth stays on admin routes. Gate any future product WS exposure. |
| RT-101 client overlap/retry policy | OTHER PHASE | HA/client work in Phase 10, following F9.6 continuity rules. |
| RT-101 product WS/cohort/notification composition | OPEN NONBLOCKING | Optional under F9.7A pending approved need; none is CURRENT. Must be server-side product work before any HA WS consumption. |
| Luma 250 ms and Hydro 1000 ms action waits | OPEN NONBLOCKING | Provisional product scheduling parameters, not accepted platform latency guarantees. |
| Hydro full actuator/sensor HIL and Doser physical pumps | OTHER PHASE | Not performed; Web/OTA bare-board HIL cannot prove physical actuation or dosing. |
| SYS-107, MNT-102 and Doser-local OTA | OTHER PHASE | System restart/common maintenance and shared OTA remain transitional/future; Phase 11 owns common OTA. |

SEC-101 being OPEN does not alone invalidate the scoped Core transport/HIL or existing HTTP cutovers. It does prevent treating the test fixture's unauthenticated WS as an accepted product security policy. Product Realtime must pass its own Auth/visibility gate before exposure.

## Phase-9 exit checklist and decision

| Accepted F9.1 step | Result |
| --- | --- |
| F9.2: one native HTTPD owner and bounded registration/lifecycle | PASS |
| F9.3: typed safe published HTTP projections | PASS |
| F9.4: owned bounded action path and response lifetime | PASS |
| F9.5: WS publication, runtime identity and sequence capability | PASS in Core |
| F9.6: coherent recovery, reconnect and measured backpressure/HIL | PASS in Core; final RT-101 policy OPEN NONBLOCKING as explicitly deferred by F9.6A/D |
| F9.7: migrate/audit Luma, Hydro, Doser production Web route families | PASS; one native HTTP owner each, Doser OTA retained |
| F9.8: integration and closure audit after validation | PASS in F9.8A; final Phase-9 status checkpoint remains F9.8B |

**Final decision: A — PHASE 9 CLOSABLE NOW.** Requiring one product WS integration as an additional Phase-9 exit gate would contradict F9.7A's explicit optional composition rule and add a criterion not present in the accepted F9.1 sequence. This verdict certifies Core Realtime capability and product native HTTP cutovers, **not** production Realtime in Luma, Hydro or Doser. The missing external wire and product cohort must be resolved before a product/HA WS launch. Phase 10 remains strictly the HA HTTP/WS client, Config Flow, discovery, entity mapping and client reconnect/resync; it does not inherit an unimplemented server foundation.

**Exact next step:** F9.8B FINAL PHASE-9 CLOSURE CHECKPOINT — docs/integration audit only: verify this evidence and unchanged production state, then mark Phase 9 CLOSED in the status documents if still valid. Do not implement Realtime or start Phase 10 in F9.8B.

Current-status wording corrected by F9.8A: `ROADMAP.md` previously said Web was solely synchronous legacy and WS did not exist, and said Realtime implementation still remained; `PROJECT_MATRIX.md` described Core Web only as legacy. Older F9.1/F9.6A/F9.7A gate documents retain their historical baseline wording. `ARCHITECTURE_VNEXT_DECISIONS.md` had `READY TO CHECKPOINT` for the now-committed F9.6 and older toolchain/product wording; this audit records the current override. `aquaOneCore/README.md` still contains legacy product usage/example wording and remains a separate documentation cleanup, not a closure blocker.
