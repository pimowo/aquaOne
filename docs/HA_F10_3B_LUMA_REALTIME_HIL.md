# F10.3B — Luma server Realtime HIL

**Date:** 2026-10-10
**Verdict:** READY FOR CHECKPOINT
**Scope:** production Luma Realtime server, recovery/recycle and compatibility
with the existing F10.2 Home Assistant polling integration

## Implemented contract

Production Luma now creates a fresh early-boot `RuntimeIdentity`, publishes one
coherent status cohort, exposes `/ws/realtime`, adds the matching Realtime
watermark to `/api/lumasense/status`, and advertises API 1.1 only after the
Realtime composition starts successfully. Semantic changes issue contiguous
sequences and send bounded TEXT JSON `luma_status_changed` notifications;
incidental local-time and RSSI refreshes retain the current position.

Application-owned recovery services per-client Closing state, sticky cohort
recovery and bounded HTTPD stop/republish/restart. Core additionally handles
WebSocket PING, PONG and CLOSE control frames without exposing product state or
commands.

## Validation evidence

- Luma Unity: 16/16 physical tests passed on the classic ESP32 after correcting
  the local 64-bit assertion helper.
- Recovery fixture scenarios R1–R10 passed on the classic ESP32. The final R4
  ran twice and proved send failure to Closing, two controlled close failures,
  a subsequent successful close, slot release, generation protection, an
  unaffected peer, unchanged sequence and no unnecessary global recovery.
- Same-port HTTP and WS, TEXT StreamStart, coherent HTTP/WS watermarks,
  contiguous notifications, reconnect/resync, runtime change after reboot,
  controlled failure recovery and repeated recycle were exercised physically.
- The production Luma build passed and the non-instrumented firmware was
  restored. Storage, Wi-Fi, NORMAL/profile 1, both HTTP endpoints, API 1.1 and
  WS StreamStart were healthy, with no observed panic, watchdog or brownout.
- An instrumented production Luma variant measured three real recycle cycles.
  `tryStop()` took 101.298–101.719 ms; republish took 0.462–0.517 ms; restart
  `begin()` took 3.429–3.522 ms. The maximum measured interval between
  `FirmwareApp::update()` calls was **108.246 ms**.
- Across the three measured cycles, free internal heap did not show a monotonic
  decline; minimum free heap stayed at 198,916 bytes and the largest free block
  stayed at 110,580 bytes. This bounded run is not a long soak test.
- In the real Home Assistant UI, the operator verified one Luma device, 13/13
  available sensors and no duplicates. Power loss made all sensors unavailable;
  after reboot all recovered automatically through the existing polling client.
  The F10.2 client accepted the additive API 1.1 response.

Generated fixture JSON and timing logs remain under the ignored `reports/`
directory and are not checkpoint artifacts.

## Recorded limits

- Physical lamp smoothness is **NOT VERIFIED** because the test board had no
  lighting power stages or loads.
- Actual Home Assistant Entity Registry `unique_id` values were not read back
  and compared. DeviceId remained stable and the implementation still derives
  the same 13 IDs from `device_type:device_id` plus fixed entity keys.
- No long-duration heap soak test was run.
- The maximum measured production-loop interval during recycle was 108.246 ms.
  Time-based transitions catch up on the next update and hardware PWM retains
  its last value during the pause; no stricter Luma loop deadline is specified.

## Status

F10.3B is **CLOSED/CURRENT**. F10.3C, the bounded Home Assistant Realtime client
with resync, safety polling and polling fallback, is **REQUIRED NEXT** and is not
implemented by this checkpoint.
