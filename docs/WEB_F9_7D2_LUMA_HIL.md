# F9.7D2 — Luma native Web HIL

**Date:** 2026-10-06
**HEAD before checkpoint:** `ca8516db7651a5265531ae40f7815472f7aa55e4` plus local F9.7D changes
**Board/profile:** ESP32-D0WD-V3 rev. 3.1, `LUMASENSE_HW_LOLIN32_TEST`
**Serial port:** `COM6`
**Device IP:** `192.168.1.57`
**Toolchain:** pioarduino 53.03.13, Arduino-ESP32 3.1.3, ESP-IDF 5.3.2

## Result

**HIL PASS.** Production `aquaOneLuma` firmware built and uploaded successfully.
Boot reported Hardware, RTC and Storage as OK, native routes as OK, and Web server READY.
Wi-Fi connected and the device served HTTP at `http://192.168.1.57/`.

The boot and post-HIL serial captures contained no panic, Guru Meditation, watchdog,
unexpected reset, heap-corruption, assert, or HTTPD fatal marker.

## HTTP matrix

| Request | Result |
|---|---|
| `GET /`, `/control`, `/diagnostics`, `/system` | 200; expected shell/content/API fetch markers present |
| `GET /assets/aqua.css` | 200; non-empty |
| `GET /definitely-not-existing` | 404; `Not Found` |
| `GET /api/system` | 200 JSON; required identity/version/uptime/restart fields present |
| `GET /api/diagnostics` | 200 JSON; system/time/storage/network structures present |
| `GET /api/lumasense/status` | 200 JSON; all required fields, two arrays of 8 values, no password/SSID/token/key/auth field |
| malformed mode POST `{bad` | 400; following status GET 200 |
| exactly 512-byte malformed POST body | 400; transport accepted the body and route parser rejected it |
| exactly 513-byte malformed POST body | 413 `Payload Too Large`, confirmed by raw TCP request; following status GET 200 |
| partial body then disconnect | sent; following status GET 200 |

The managed host HTTP client did not surface a status for its first 513-byte request,
but a raw TCP request with the same exact `Content-Length: 513` received
`HTTP/1.1 413 Payload Too Large`. The device remained responsive in both cases.

## Actions and persistence

Original state was `NORMAL`, profile 1. The test used profile 2 and restored profile 1.

| Action | Result |
|---|---|
| `SERVICE` | 200 `applied`; status `SERVICE` |
| repeat `SERVICE` | 200 `no_change` |
| MANUAL, eight zero levels, 15 min | 200 `applied`; status `MANUAL`, arrays length 8 |
| `EXIT_MANUAL` | 200 `applied`; status returned to `SERVICE` |
| select profile 2 | 200 `applied`; status profile 2 |
| repeat profile 2 | 200 `no_change` |
| restore profile 1 | 200 `applied`; status profile 1 |
| invalid profile 6 | 400; profile remained 1 |
| invalid manual timeout 5 | 400 |
| final mode restore | 200 `no_change`; final state `NORMAL`, profile 1 |

Each host action request was sent once. No automatic POST retry was used.

## Latency and stability

Twenty sequential safe `POST {"mode":"NORMAL"}` no-change requests returned 200;
no response was 202 `outcome_unknown`.

| Count | Min | Median | p95 | Max |
|---:|---:|---:|---:|---:|
| 20 | 71.570 ms | 249.802 ms | 504.565 ms | 561.396 ms |

A near-concurrent no-change POST and status GET both returned 200; completion time for
the pair was 249.340 ms. A 60.3-second light soak made 47 requests (`/api/lumasense/status`
and periodic `/api/system`), with zero failures and monotonic uptime.

Port 80 was reachable; direct checks of ports 81 and 8080 found no listener. Runtime free
heap is **NOT AVAILABLE WITHOUT INSTRUMENTATION**.

## Residual risks

- The provisional 250 ms Application wait produced no 202 responses, but p95 and maximum
  host-observed HTTP latency were above 250 ms. Preserve this measurement for later
  scheduling and HTTPD latency analysis.
- The synchronous HTTPD wait can delay another request on the same server task; the
  concurrent request sanity check remained successful.
- Luma remains polling-only. WEB-101 is DECISION REQUIRED and SEC-101 remains OPEN.
