# F9.7E1 — Hydro native Web migration gate

**Verdict: PASS.** This is a docs-only audit. No source, test, PlatformIO,
flash, HIL, staging, commit, or push change is part of E1.

## Current inventory

Hydro uses legacy `WebService` / `Esp32WebBackend`. It has ten endpoint-method
pairs: GET `/`, `/assets/aqua.css`, `/api/system`, `/api/diagnostics`,
`/control`, `/settings`, `/diagnostics`, `/api/hydrosense`; POST `/api/control`
and `/api/settings`; and a 404 fallback. Native Core supplies CSS, system and
diagnostics; the other seven product routes retain path and method.

Current toolchain is unpinned `platform = espressif32`, `esp32-s3-devkitc-1`,
Arduino, 4 MB flash, `default.csv`, DIO, 80 MHz, and the existing S3/USB flags.
E2 pins pioarduino 53.03.13 without changing board, framework, flash, partition
or flags.

## Control boundary

Current `POST /api/control` accepts `action=service_toggle`, `mute`, or
`reset_lockout`, returns plain `200 OK`, and returns `400` for missing,
malformed, or unknown input. It directly reads/calls `TopupController` and
`BuzzerController` in HTTPD.

E2 parses a fixed owned `HydroControlRequest` enum
`{ ToggleServiceMode, MuteBuzzer, ResetLockout }`, submits it through a bounded
`WebApplicationBridge`, and executes it in Hydro Application. Toggle carries no
boolean: the executor reads current state and toggles there. This prevents an
HTTPD read-then-set race. `reset_lockout` itself turns the pump off but may
later permit automatic pumping, so HIL needs a disconnected pump or equivalent
physical prevention.

## Settings fields and exact body calculation

All normal fields are required; unchecked checkboxes are absent and mean false.
Password replacements are optional. Decoded capacities exclude NUL.

| Keys | Type / cap | Checkbox | Secret |
|---|---:|---|---|
| `floatActiveLow`, `floatUsePullup`, `staEnabled`, `autoReconnect`, `apEnabled` | bool `1` | yes | no |
| `floatDebounceMs` | u32, 4 digits | no | no |
| `ultrasonicMinDistanceCm`, `ultrasonicMaxDistanceCm`, `tankEmptyDistanceCm`, `tankFullDistanceCm`, `reserveLowPercent`, `reserveCriticalPercent`, `reserveHysteresisPercent` | float, 16-byte lexical cap | no | no |
| `ultrasonicTimeoutUs` | u32, 6 digits | no | no |
| `tankSampleIntervalMs` | u32, 5 digits | no | no |
| `tankMaxFailedSeries` | u8, 3 digits | no | no |
| `topupStartDelayMs`, `reconnectMs` | u32, 6 digits | no | no |
| `topupMaxPumpRuntimeMs` | u32, 7 digits | no | no |
| `ssid`, `hostname`, `apSsid` | text, 32 bytes | no | no |
| `password`, `apPassword` | text, 64 bytes | no | yes |

The current float parser has no lexical cap, so E2 must impose the stated
16-byte cap before numeric parsing; storage validation remains authoritative for
ranges and cross-field constraints. The independent byte sum is: 364 key bytes
+ 24 `=` + 23 `&` + 378 ASCII value bytes = **789 B**. The five string fields
contain up to 224 of those value bytes; replacing each with three-byte `%XX`
encoding changes the value subtotal to 826 B, hence 364 + 24 + 23 + 826 =
**1,237 B**. Select **1,536 B** for `/api/settings`, leaving 299 B margin.

Core's normal capacity is 512 B, so E2 must raise it to 1,536 B, growing the
owner-held `EspIdfWebTransport::normalBody_` by **1,024 B**. Do not reduce form
or config capacities. `/api/control` keeps its small explicit limit. E3 HIL
must check exact 1,536 B, 1,537 B -> `413`, then a successful following request.
This is a global implementation capacity, not a platform standard and not an
OTA/upload limit. It adds the same 1,024 B to every `EspIdfWebTransport`, also
products whose routes need only 512 B. For Luma, the previously measured static
RAM would rise approximately from 60,820 B to 61,844 B out of 327,680 B, still
leaving about 265,836 B. A template capacity would spread transport types and a
runtime/external buffer would complicate ownership; the global constant is the
smallest E2 change and its cross-product cost is accepted provisionally.

## Secrets, request ownership, validation

Current SettingsPage places `wifiPassword` and `wifiApPassword` in HTML
`value` attributes. `type=password` does not protect them; this is the current
SEC-002 violation.

Target GET settings uses a bounded non-secret projection. Password inputs are
always empty and may expose only configured booleans. Missing or empty POST
password key means **keep existing secret**; a nonempty decoded value means
**replace it**. No masked magic value and no clear-secret action. Disabling STA
or AP does not clear stored credentials.

`HydroSettingsRequest` is trivially copyable and self-contained: editable
booleans/numbers, `char[33]` SSID/hostname/AP SSID, `char[65]` password
replacement buffers, and explicit replace flags. No pointers, `String`, live
config, storage, or HTTP objects.

Application copies active config, applies non-secrets and flagged replacements,
calls a public pure `HydroSenseConfigStorage::validateConfig()` adapter,
persists candidate, then commits active config and schedules restart only after
successful save. `save()` also invokes the registered storage payload validator,
but persistence is not the sole validation. HTTPD has no config, storage,
controller, or restart access.

## Results and restart

Retain UI-compatible plain `200 OK`. Application then preserves current safe
restart order: pump off, wait 500 ms, pump and buzzer off, `ESP.restart()`.
HTTPD never restarts.

| Condition | Response |
|---|---|
| malformed/lexical parse or completed validation failure | 400 |
| completed storage failure | 500 |
| bridge queue full | 503 |
| accepted wait timeout or failed completion channel | 202 outcome unknown |
| completed success | 200 `OK` |

An accepted request is never claimed cancelled. The Settings frontend must
distinguish completed `200` from `202` outcome unknown. Its current generic 2xx
handling must not announce a confirmed save or restart after `202`, and must
not automatically retry an accepted request.

## Projection and timing decisions

Dashboard and Control read `SystemStatus`; Settings reads config; Diagnostics
reads system, network, storage and status. All four are **NEEDS_PROJECTION**.
`/api/hydrosense` serializes a local status snapshot. `SystemStatus` contains
scalars, enums and `IpAddress`, so it may be the snapshot payload after E2 adds
trivial-copyability/no-pointer checks; otherwise create a minimal
`HydroStatusProjection`. Settings projection excludes password bytes.
The diagnostics projection must preserve the current page's identity/version,
restart reason and uptime, network state/IP/RSSI/reconnect/AP facts, storage
backend/validity/slot/generation/load/save facts, and Hydro sensor, pump,
service and alarm facts. It performs no live reads while rendering.

Hydro stays polling-only; no current UI needs WebSocket. `WaterTank::update()`
can call `pulseIn()` through UltrasonicSensor and block up to the validated
100,000 us timeout. Topup and Buzzer are nonblocking state machines; Preferences
save duration is unmeasured. Use a route-local provisional bridge wait of
**1,000 ms**, not a Core global policy. It covers the Luma 561.396 ms observed
tail, Hydro's 100 ms sensor wait and margin; S3 HIL decides the final value.

## Test, S3, and risk plan

Add `test_web_native_hydro`: routes/pages/404, snapshot JSON, actions,
Application-side toggle, queue-full, timeout, isolation and HTTP failure
autonomy. Config cases: blank/missing secret retention, replacement, no secret
rendering/API output, invalid candidate non-persistence, storage failure leaves
old config, successful save commits, restart only after success.

E2 validates a production ESP32-S3 build with pinned platform and unchanged
flags. E3 performs S3 HIL with the pump physically disconnected or equivalently
prevented from turning on. Mute is safe; service-toggle and reset-lockout need
that prerequisite. Avoid credential replacement unless network recovery/IP loss
is explicitly planned. Test body boundaries, non-network save/restart, blank
secrets, errors and post-restart recovery.

The repository does not vendor `default.csv`, and current platform is unpinned.
The normal espressif32 default commonly has OTA metadata and two app slots, but
actual OTA capacity must be inspected after pinning in E2; no partition edit in
E1. WEB-101 and SEC-101 remain open. SEC-002 is a mandatory E2 acceptance
criterion: no page/API projection returns existing password bytes.

The delayed restart is a transitional Hydro product workflow. It does not
implement or close SYS-107 or a shared Core restart framework.
