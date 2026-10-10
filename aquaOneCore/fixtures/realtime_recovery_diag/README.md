# F10.3B-HIL-2-DIAG: isolated recovery fixture

This is a separate classic ESP32/Arduino firmware. It links the current AquaCore
library and runs one real ESP-IDF HTTPD listener on port 80. It does not compile
or register Luma product routes and does not change Luma firmware. The WS
endpoint is `/ws/hil`; it emits the F10.3A TEXT `stream_start` and
`notification` schema. Diagnostic HTTP endpoints exist only in this fixture.

**Physical status (2026-10-10):** R1–R3 and R5–R10 passed in the previous
HIL session. R4 passed twice after the fixture correction; see the new
`reports/diag_20261010_204531.json` and `diag_20261010_204557.json`.
The earlier R4 FAIL reports remain unchanged.

## Fault boundary and observability

The fixture-local PlatformIO environment links `--wrap` around five actual
ESP-IDF calls used by Core: `httpd_queue_work`,
`httpd_ws_send_frame_async`, `httpd_sess_trigger_close`, `httpd_stop`, and
`httpd_start`. Each fault is armed for a bounded number of calls and records
an observed hit. No Core public API or production binary receives a hook.
R7 fails the fixture's resource-build callback once. R1 is different: the
HTTPD callback is held for 750 ms while the Application loop publishes into
the real four-slot Core work pool; the runner requires an actual Core `Busy`
return, never a manually incremented counter. R5 first obtains a real Core
`RetryNeeded` for a Closing client, then uses the same 10 s retry rule as
Luma's supervisor. During the ensuing fixture-only `httpd_stop` wrapper,
one synchronous Core `serviceRealtimeRecovery()` call observes the brief
`server_ == nullptr`/Closing window and must return the actual
`TransportRecycleSuggested` enum. This probe is diagnostic; the 10 s rule
already initiated the recycle. The Application-side handler records and
handles that actual result by retaining StopPending. It does not call stop
from HTTPD or concurrently reenter the lifecycle owner.

`/api/hil/snapshot` is a copied `RealtimeSnapshot` and is the authoritative
HTTP watermark. `/api/hil/status` exposes publisher results. `/api/diag/status`
exposes bounded fault-hit counts, recovery/stop/restart attempts, retry gaps,
before/after/stable heap samples, and the scenario phase. These diagnostic
routes do not appear in production Luma. Only the Application loop mutates
the cohort and owns stop/begin; HTTPD callbacks set atomic requests or read
published snapshots.

Phase values: 0 idle, 1 armed, 2 recovery, 3 stop pending, 4 republish pending,
5 begin pending, 6 stabilizing, 7 complete, 8 failed. `result` is 0 pending,
1 locally completed, 2 failed. The host still performs independent HTTP/WS
checks before reporting PASS. Scenarios have a device-side 30 s deadline;
the runner's default deadline is 38 s.

| ID | Trigger and exact injection | Observed PASS condition | FAIL and return to normal |
| --- | --- | --- | --- |
| R1 Busy | `POST /api/diag/run` body `R1`; 32 bounded transitions while HTTPD sleeps 750 ms | Real Core `Busy` count rises; sticky recovery disconnects old WS, republishes same current position without extra sequence, clears and reconnects | Missing Busy/clear/HTTP/WS or timeout; reboot fixture if failed |
| R2 QueueWorkFailure | `R2`; next actual `httpd_queue_work` call returns ESP_FAIL | Wrapper hit and Core `queueFailure` rise; cohort recovery/StreamStart/HTTP agree | Missing exact hit or recovery; reboot fixture if failed |
| R3 Sticky recovery | `R3`; explicit Core `requestRealtimeRecovery()` | Old client closes, publisher republish and Core clear succeed at same sequence without Domain change | Sticky remains or sequence grows; reboot fixture if failed |
| R4 Single-client Closing | Two WS clients, then `R4`; one outgoing send and the first two close requests fail | First close failure is consumed in the HTTPD send callback; Application service observes the second failure as RetryNeeded, then succeeds on its next attempt. The real Core slot/generation is traced from Live to Closing and released; peer receives the position-1 notification and answers PING, with no sticky recovery or cohort republish | Peer affected, missing retry/cleanup, extra sequence, or timeout; reboot fixture if failed |
| R5 Recycle suggested | `R5`; outgoing send fails, bounded 200 close requests fail; after 10 s retry rule initiates stop | Actual Core `TransportRecycleSuggested` returned by controlled stop-window probe; stop/republish/begin, HTTP/WS recover | Missing RetryNeeded/probe enum/recycle; reboot fixture if failed |
| R6 Stop failure | `R6`; first actual `httpd_stop` returns ESP_FAIL | `tryStop()` remains pending; second attempt after about 1 s succeeds; listener recovers | No injected hit, no retry, or wrong retry cadence; reboot fixture if failed |
| R7 Republish failure | `R7`; first resource build after successful stop fails | One build hit, at least two republish attempts about 1 s apart, restart only after coherent republish | Stale HTTP/WS or no retry; reboot fixture if failed |
| R8 Begin failure | `R8`; first actual `httpd_start` returns ESP_FAIL | `NativeWebService.begin()` remains pending; retry about 1 s later succeeds | False success/duplicate listener/no retry; reboot fixture if failed |
| R9 Successful recycle | `R9`; real `tryStop`, Core republish, `NativeWebService.begin` | HTTP snapshot and new WS StreamStart agree; runtime/sequence stable; reconnect works | HTTP/WS unavailable or mismatch; reboot fixture if failed |
| R10 Repeated recycle | Runner issues `R10` three bounded times | Three real cycles, coherent HTTP/WS after each, recorded heap and timings | Any cycle fails; reboot fixture if failed |

For R1–R4 and R5 the Application recovery servicing is independent of another
Domain transition. R4 uses two clients and checks the surviving one. The
runner records actual HTTP responses and TEXT frames; no scenario is marked
PASS from a fault-arm flag alone. `HEAP` samples 0, 1, 2 and one nonreading
client plus five reconnects. It does not claim to have induced backpressure.

## Timing and heap scope

`esp_timer_get_time()` supplies monotonic microsecond durations for fixture
loop execution and inter-loop gap, `serviceRealtimeRecovery()`, `tryStop()`,
cohort republish, and `NativeWebService.begin()`. Separate fixed-size sample
rings cover baseline, recovery, and recycle. `/api/diag/*-metrics` returns
total count, retained count (up to 128), minimum, median, maximum, and context.
No arbitrary latency PASS threshold is applied. Retry cadence is checked
against the configured 1 s supervisor interval.

**Representativeness limit:** this firmware does not instantiate Luma
`FirmwareApp`. Its `fixture_loop` and `fixture_loop_gap` are *not* production
`FirmwareApp::update()` timing. A separate production-side timing witness is
required before concluding that lamp scheduling remains within its own
requirements during HTTPD stop/restart. Host HTTP latency is never used as
Domain timing evidence.

`heap_caps_get_free_size`, `heap_caps_get_minimum_free_size`, and
`heap_caps_get_largest_free_block` sample internal heap before, immediately
after, and after 500 ms stabilization for each scenario. The runner also
samples different client counts and reconnects. A single heap decrease is
reported as a measurement, not automatically called a leak.

## Build and next physical session

Prerequisite: a local ignored `secrets.h` defining `WIFI_SSID` and `WIFI_PASS`.
This environment can reuse the existing ignored F9.6 secrets via its include
path; on a fresh checkout provide `src/secrets.h` locally. Never commit it.
The host needs Python `pyserial` 3.x. It creates a uniquely named JSON file
only under this fixture's ignored `reports/` directory. The runner pulses
EN via COM control lines on startup because opening this CH9102 port alone
does not emit a fresh `HIL_READY` marker.

From repository root, build without upload:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d aquaOneCore/fixtures/realtime_recovery_diag -e esp32_recovery_diag
```

In the **next authorized HIL session**, first verify that the selected COM
port is the bare classic ESP32-D0WD-V3 rev. 3.1 with 4 MB flash and no loads
on the output GPIOs. Record its USB identity and MAC. Only then upload the
fixture to that same port (no erase_flash or NVS erase). Use an explicit IP,
TCP port, and COM port; opening COM may reboot the fixture, so the runner waits
for its `HIL_READY` serial marker and HTTP before testing:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device list
python -m esptool --port COM6 flash_id
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d aquaOneCore/fixtures/realtime_recovery_diag -e esp32_recovery_diag -t upload --upload-port COM6
python -B aquaOneCore/fixtures/realtime_recovery_diag/host_runner.py --host 192.168.1.57 --tcp-port 80 --com-port COM6 --scenario ALL
```

Use `--scenario R1` through `R10` or `HEAP` for a bounded single case. Review
`reports/diag_*.json`, including FAIL/NOT RUN states, serial fault markers,
heap, and timing distributions. After testing, restore the production Luma
firmware with the current Core to the verified board, then check normal boot,
Wi-Fi, HTTP `/api/system` and `/api/lumasense/status`, API 1.1 and WS. Do not
run on a board connected to lighting power stages.

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d aquaOneLuma -e esp32dev -t upload --upload-port COM6
```
