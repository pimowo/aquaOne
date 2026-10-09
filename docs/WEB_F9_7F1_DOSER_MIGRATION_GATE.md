# F9.7F1 — Doser native Web migration gate

> Status: F9.7F1 and F9.7F2 are CLOSED. F9.7G is REQUIRED NEXT before atomic Doser production cutover.

> F9.7G1 design gate: docs/WEB_F9_7G1_DOSER_OTA_DESIGN.md resolves the native streaming API, product multipart/Update ownership, cancellation, restart and one-listener cutover plan. This is design only; production still uses legacy Web.

> F9.7F2 implemented and tested TOOLCHAIN-2, callback-scoped native header/Basic Auth, an Application-owned restart bridge, published Core projections and authenticated `GET /update`. Production remains LEGACY CURRENT. Native `POST /update` is absent, so F9.7G still blocks cutover. Doser's two storage records have no single Core storage status; the foundation reports storage health as `unknown`. Production Toolchain/Storage/GPIO sanity HIL passed on a bare ESP32-S3: no physical pumps or output stages were connected, native routes were not tested on hardware, exact pre/post pump-field equality was not captured, and the final NVS state was stable with no observed Storage failure or default reset.

## CURRENT production Web

`main.cpp` composes one physical legacy owner:

```text
Esp32WebBackend -> WebService -> WebManager
```

`webService.update()` runs in the main loop before `WebManager::loop()`, scheduler, diagnostics and MQTT. This is LEGACY CURRENT and remains the sole Doser production listener through F9.7F. There is no Doser native HTTPD, WebSocket or Realtime endpoint.

Legacy `WebService` owns Core built-ins: `GET /`, `GET /assets/aqua.css`, `GET /api/system`, `GET /api/diagnostics` and 404. The first two are HTML/CSS; the APIs are current legacy JSON read paths.

`WebManager` owns exactly:

| Method | Path | Auth | Request / response | Effects and concurrency |
| --- | --- | --- | --- | --- |
| POST | `/api/restart` | Basic Auth | Bodyless; `202` plain text. | Schedules restart; every accepted duplicate resets the deadline. |
| GET | `/update` | Basic Auth | Bodyless; `200` HTML update form. | No mutation. |
| POST | `/update` | Basic Auth at START | Arduino multipart; form sends `X-Firmware-Size`; final handler returns `200` plain text on success or `500` plain text with captured error. | OTA, maintenance, pump stop, delayed restart. |

There is no current `GET /api/status`; F9.7F must not invent it. Doser user and HA state remain MQTT/Discovery until F10.6.

## CURRENT Basic Auth and restart

`WebManager::authenticateAdmin()` returns `503` and disables administration when the password pointer is absent, empty or a default placeholder. Otherwise it validates Basic Auth. Invalid or absent credentials receive a Basic challenge with realm `PMW AquaDoser`. Credentials stay product-owned `WebManager` inputs; this document records no values.

After authorized `POST /api/restart`, code completes the `202` response and calls `scheduleRestart()`. Main-loop `WebManager::loop()` acts after exactly `RESTART_DELAY_MS == 1000 ms`: it clears pending, calls `stopPumps()`, then `restartDevice()`. Therefore the response precedes restart and this path cannot restart without stopping pumps.

Duplicate restart requests are neither busy nor idempotent: each authorized request is accepted and overwrites `restartAt`. The `409 busy` wording in `WEB_STANDARD.md` is TARGET/W2, not CURRENT. F9.7F preserves this compatibility until a separate decision changes it. Native HTTPD may authenticate, frame a bounded request and report a semantic result; restart intent, pending state, pump safety and execution belong to serialized Application/system ownership. SYS-107 remains open.

## CURRENT OTA and terminal states

`GET /update` uses the same Basic Auth and serves local HTML. The form requires `.bin`, sends multipart `firmware` and `X-Firmware-Size`. Legacy upload callbacks provide START, CHUNK, END and ABORT; `POST /update` writes the final text result.

Authorized START resets prior upload state, validates `.bin` and declared size against `availableFirmwareSpace()`, calls `stopPumps()`, sets scheduler OTA-in-progress, then calls `Update.begin(expectedSize, U_FLASH)`. CHUNK calls `Update.write`, accounts bytes and logs at 25 percent steps. END requires exact byte count then calls `Update.end(true)`. Success retains OTA/maintenance until delayed restart. `failOta()` aborts an accepted Update, clears OTA state and scheduler flag, and records the final `500` error.

| Terminal path | Update / OTA state | Scheduler / pumps | Restart / response |
| --- | --- | --- | --- |
| START unauthorized | No operation; existing active session unchanged. | Unchanged. | No callback response; final route auth challenge is separate. |
| START invalid filename or size | No `Update.begin`; error stored. | Scheduler false; pumps not stopped. | No restart; final `500`. |
| START begin failure | Cleared by `failOta()`. | Scheduler cleared; pumps already stopped. | No restart; final `500`. |
| CHUNK write failure | Accepted Update aborted; state cleared. | Scheduler cleared; pumps remain stopped. | No restart; final `500`. |
| END size mismatch | Accepted Update aborted; state cleared. | Scheduler cleared; pumps remain stopped. | No restart; final `500`. |
| END failure | Accepted Update aborted; state cleared. | Scheduler cleared; pumps remain stopped. | No restart; final `500`. |
| ABORT after authorized START | Accepted Update aborted; state cleared. | Scheduler cleared; pumps remain stopped if START reached it. | No restart. |
| Disconnect | No explicit policy beyond an Arduino ABORT callback. | Cleanup is proven only when ABORT is delivered. | F9.7G gap. |
| Successful END | `Update.end(true)` succeeded; OTA active to reboot. | Scheduler remains OTA; pumps stopped at START. | 1000 ms restart; final `200` first. |

A second authorized START currently calls `resetUploadState()`: it aborts the accepted transfer and begins replacement, without `409 busy`. That is CURRENT behavior; a native busy policy requires a decision, not an unannounced F9.7F change.

`DoserWebRuntime::serviceDuringUpload()` runs PumpDriver, Wi-Fi, time, scheduler, diagnostics, MQTT and `yield` in the legacy upload callback. It must not move into HTTPD. F9.7G needs bounded chunk handoff to one serialized Application/maintenance owner for Update, scheduler, pump safety, timeout/disconnect cleanup and restart.

## Atomic cutover and scope split

Multipart OTA depends on Arduino `WebServer` lifecycle. Doser cannot partly cut production over to `NativeWebService` while `/update` remains legacy. Two servers, different ports, a legacy OTA-only listener and silent OTA removal are forbidden. One physical owner is mandatory.

| Gate | Scope | Production owner |
| --- | --- | --- |
| F9.7F | TOOLCHAIN-2 Doser; native built-in projections; normal/admin adapters; Basic Auth boundary; restart Application bridge; semantic tests. | Legacy server remains sole owner. |
| F9.7G | Streaming OTA; maintenance/system ownership; backpressure; START/CHUNK/END/ABORT; disconnect/timeout cleanup; pump/restart safety; atomic cutover; physical S3 OTA HIL. | Native owner atomically replaces legacy owner. |

If OTA is deferred to Phase 11/MNT-102, the entire Doser production cutover is deferred too. Recommended path: F9.7F + F9.7G + one atomic cutover. This does not add a shared Core OTA framework; common OTA remains Phase 11/MNT-102.

## Toolchain and partition gate

Current identity is `esp32-s3-devkitc-1` for ESP32-S3 Super Mini, 4 MB flash, `default.csv`, DIO, upload 921600, and native USB flags `ARDUINO_USB_CDC_ON_BOOT=1` and `ARDUINO_USB_MODE=1`. TOOLCHAIN-2 target is pioarduino 53.03.13, Arduino-ESP32 3.1.3, ESP-IDF 5.3.2; preserve board identity and flags.

| Partition | Offset | Size |
| --- | ---: | ---: |
| NVS | `0x9000` | `0x5000` / 20 KiB |
| otadata | `0xe000` | `0x2000` / 8 KiB |
| ota_0 | `0x10000` | `0x140000` / 1,310,720 B |
| ota_1 | `0x150000` | `0x140000` / 1,310,720 B |
| SPIFFS | `0x290000` | `0x160000` / 1,441,792 B |
| coredump | `0x3f0000` | `0x10000` / 64 KiB |

The F9.7F2 production image is 1,085,528 B, leaving 225,192 B (about 17.18%) in one 1,310,720-byte OTA slot. F9.7G must remeasure image size and headroom after implementing native OTA.

## Native gaps, tests and security

F9.7F2 adds the smallest Core-neutral capability for callback-scoped header access, Basic Auth and `WWW-Authenticate`; product passwords remain outside Core. It does not establish a platform-wide credential standard or close SEC-101.

It also lacks multipart framing, owned chunk handoff, declared-size policy, backpressure, disconnect/inactivity cleanup, END response lifecycle and fixed-memory upload semantics. F9.7G must stream only, never buffer firmware in RAM. F9.7G1 now selects a 1,024-byte chunk, partition-derived total limit and provisional timeout policy for implementation and HIL measurement. HTTPD may authenticate/frame/receive bounded chunks and transfer ownership; it must not manually run subsystem loops, mutate pumps/domain state, or directly write firmware outside the serialized owner.

`DiagnosticsManager` may provide Application-side copied projections without duplicate authority. Core `/api/system` and `/api/diagnostics` use published projections. Product `/api/status` remains a separate versioned API decision. Future pages may use aquaOne Web Theme v1; F9.7G may theme OTA only if auth/upload behavior is preserved. ERROR/MAINTENANCE permissions need explicit product policy.

Keep `test_web_bridge` semantics: auth, restart delay/pump stop, metadata, START/CHUNK/END/ABORT, failure cleanup and next-upload behavior. Rewrite only its legacy transport harness. F9.7F tests cover native auth before effects, current duplicate restart semantics, projections, routes, one listener, thread isolation and bridge full/timeout. F9.7G tests cover invalid START, partition/size checks, partial/multi chunks, write/end failure, ABORT, disconnect, timeout, concurrent START/backpressure, cleanup, pump safety, maintenance, success/restart/reconnect and bounded memory.

F9.7F2 passed TOOLCHAIN-2 compile, native semantics and bare-board production Toolchain/Storage/GPIO sanity HIL. F9.7G needs safe physical S3 OTA HIL: valid image, invalid metadata/size, abort/disconnect, upgrade, boot/version verification and Web recovery. SEC-101 remains open. Basic Auth must be preserved and OTA remains authenticated, but does not solve platform-wide SEC-101. MQTT, `MqttManager` and `HaDiscovery` remain LEGACY CURRENT until F10.6.

## Decision

**F9.7F1 and F9.7F2 CLOSED. F9.7G is mandatory before atomic Doser production cutover.**
