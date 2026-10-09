# F9.7G1 — Doser native streaming OTA and atomic Web cutover

**Status:** F9.7G1 design gate and F9.7G2 Core streaming HTTP foundation are CLOSED. G3-G5 behavior in this document remains design, not implemented behavior. F9.7G3 is required next; Phase 9 remains in progress. Production Doser remains on legacy Web.

## Scope and current baseline

Production Doser still composes one Esp32WebBackend, WebService and WebManager; the main loop calls
webService.update(). F9.7F2 supplies tested native read/admin routes and an Application-owned restart
foundation, but none is composed into production. F9.7G will replace the legacy listener atomically with one
EspIdfWebTransport plus NativeWebService on port 80 and add authenticated streaming POST /update. There is no
intermediate two-listener, second-port or OTA-only legacy configuration. Doser MQTT/Discovery remains legacy
until F10.6.

Current legacy GET /update authenticates and serves the 1,528-byte page. Its FormData form sends one firmware
field to POST /update and sets X-Firmware-Size to file.size. On authorized upload START, WebManager clears any
previous upload, checks the case-insensitive .bin suffix, parses size with Arduino String.toInt(), compares it
with ESP.getFreeSketchSpace(), stops pumps, sets scheduler OTA state, then calls Update.begin(size, U_FLASH).
CHUNK calls serviceDuringUpload() and Update.write(); END checks byte count and calls Update.end(true);
ABORT/failure calls Update.abort() if accepted and clears the scheduler flag. Success responds 200 with the
exact text “Aktualizacja zakończona. Urządzenie uruchomi się ponownie.”, then uses a 1,000 ms delayed restart.
Failure responds 500 with “Aktualizacja nieudana: ” and the captured error. The legacy second authorized START
replaces the active transfer. F9.7G deliberately changes that concurrency policy to one upload only and 409
when a competing request reaches the handler while an upload is active.

The existing page, FormData field, X-Firmware-Size and response text stay unchanged. No native GET /api/status
is added. Native POST /update does not exist at this checkpoint.

## Evidence from installed toolchains

The installed ESP-IDF 5.3.2 HTTP server header and sources were inspected under the PlatformIO
framework-espidf package, alongside the ESP32-S3 headers in framework-arduinoespressif32-libs.
HTTPD_DEFAULT_CONFIG gives a 4,096-byte server task stack, recv_wait_timeout = 5 seconds, send_wait_timeout =
5 seconds, seven open sockets and eight default URI slots; the current adapter raises URI slots to 25. The
source sets SO_RCVTIMEO/SO_SNDTIMEO from those values. HTTPD dispatches URI callbacks in its server task. A
synchronous long upload handler therefore delays other URI handlers on that listener; it does not provide
parallel route execution.

httpd_req_t.content_len is the whole HTTP request body length. httpd_req_recv() can return a short positive
read, zero for peer close, or negative failure including HTTPD_SOCK_ERR_TIMEOUT (-3). The installed header
says chunked request encoding is unsupported. Request headers are available only during the handler and are
purged by response send; copy needed values before responding. httpd_resp_send() sends a fixed-length
response; httpd_resp_send_chunk() needs a final zero chunk. Neither proves that the browser received the
response. After a handler returns ESP_OK, httpd_req_delete() attempts to purge all remaining body bytes; on
error it fails and the session closes. Returning ESP_FAIL from a handler closes that socket without an
unbounded purge. httpd_sess_trigger_close() queues an asynchronous close and is not the primary in-handler
cleanup primitive. A close callback exists, but cleanup of an active upload is driven by the receive/error
path and the Application cancellation state, not by assuming a browser ABORT.

The installed Arduino-ESP32 3.1.3 Update implementation uses esp_ota_get_next_update_partition() for U_FLASH.
ESP.getFreeSketchSpace() returns that partition's size. Update.begin() validates the size and allocates a
4,096-byte sector buffer; write() copies into that buffer and may erase/write flash synchronously when
flushing; end(true) flushes, verifies and calls esp_ota_set_boot_partition() only after successful
finalization. Update.abort() resets its internal state and frees buffers; already written bytes in the
inactive slot remain but do not become the selected boot partition. The Update object is mutable and has no
locking in this implementation: begin/write/end/abort and errorString access belong to one serialized
Application context. Flash erase/write and end may block; no HTTPD callback invokes them. This is an
implementation observation, not proof of every power-loss point.

Source evidence: framework-espidf/components/esp_http_server/include/esp_http_server.h and src/httpd_main.c,
httpd_uri.c, httpd_txrx.c, httpd_parse.c, httpd_sess.c; Arduino libraries/Update/src/Update.h and Updater.cpp;
cores/esp32/Esp.cpp. The multipart boundary rule comes from [RFC 2046 section
5.1](https://www.rfc-editor.org/rfc/rfc2046#section-5.1); the required form-data boundary and part disposition
come from [RFC 7578 section 4](https://www.rfc-editor.org/rfc/rfc7578#section-4).

## Transport and multipart decisions

| Option | Decision |
| --- | --- |
| Add streaming methods to HttpServerTransport | Rejected: expands every normal transport and risks changing existing POST behavior. |
| Separate narrow HttpStreamingServerTransport capability on the same EspIdfWebTransport object | **Selected.** It registers a bounded raw-body route; it carries no firmware or multipart concept. |
| Product code calling raw ESP-IDF transport APIs | Rejected: leaks httpd_req_t and HTTPD task details into Doser. |
| Generic Core multipart parser | Deferred: would imply a platform policy before Phase 11. |
| Doser-local fixed multipart parser over Core raw streaming | **Selected.** Preserves the current browser wire contract. |
| Change the page to application/octet-stream | Rejected for this migration. |

The separate capability accepts method, path, borrowed handler/context and a maximum raw content length. One
physical EspIdfWebTransport owns a unified fixed route table of at most 24 HTTP routes, counting normal and
streaming entries together, plus the existing reserved WebSocket URI slot. It rejects duplicate method/path
pairs across both route kinds and reserved Core routes. Both capabilities register before begin();
registration freezes on successful begin. Failed partial registration is terminal for that instance, as in
NativeWebService. The route list is static and borrows contexts for the lifetime of all callbacks.
NativeWebService still owns its built-ins and uses the normal capability; the Doser upload adapter registers
its streaming route through the narrow capability before NativeWebService.begin().

Transport events are HTTP-neutral: BodyStart (borrowed request metadata and headers), BodyData (borrowed bytes
valid only for this call), BodyEnd (exact content_len consumed), BodyAbort (receive error, timeout,
disconnect, handler rejection or stop). The transport reads at most 1,024 bytes per httpd_req_recv() call and
never passes a raw httpd_req_t upward. An event returns Continue or Stop; the product handler owns the
response status through the existing response writer. On Stop with unread bytes, the adapter may send a
best-effort error response, then returns ESP_FAIL to close the socket and prevent HTTPD's unbounded purge.
Abort is idempotent and delivered at most once by the transport for a started request; the product
cancellation path remains safe if a separate close notification arrives. Normal routes retain
normalBody_[1537], route-specific normal body limits and their existing oversize behavior. Streaming never
uses normalBody_.

The Doser parser accepts exactly one part: Content-Disposition: form-data with name="firmware" and a nonempty
filename ending in .bin, case-insensitively. It rejects extra form fields, second parts, duplicate file parts,
preamble, epilogue, nested multipart, transfer encodings, unsupported parameters and bytes after the final
delimiter. Filename bytes are metadata only and never become a filesystem path. Reject path separators,
NUL/control bytes and length above 128 bytes; do not silently truncate. A Content-Type part header is
optional. The parser accepts only CRLF framing, not bare LF. It accepts a closing delimiter with or without
the final CRLF.

The request Content-Type must be multipart/form-data (case-insensitive media type) with exactly one boundary
parameter; optional quotes around the value are accepted. Missing/other media type,
missing/duplicate/invalid/oversize boundary and other parameters fail 400. RFC 2046 limits boundary to 1–70
characters, with no trailing space; this design uses that exact bound and the RFC bchars grammar. Content-Type
header storage is 128 bytes including NUL; overflow fails 400. The part header block is at most 512 bytes
including its terminating CRLFCRLF, each line at most 256 bytes including CRLF, and the filename at most 128
bytes. These product limits cover the current page's one file part and are explicit compatibility bounds.
Header overflow fails 400 before any file bytes are written.

X-Firmware-Size is mandatory and parsed as a strict unsigned base-10 uint32_t: 1–10 ASCII digits, value 1
through UINT32_MAX, no sign, spaces, leading/trailing garbage or overflow. Leading zeroes are allowed if the
resulting value is nonzero. The value must not exceed the Application-published availableFirmwareSpace()
snapshot, and Application Start rechecks the live value. The size is firmware bytes alone. The known 4 MB
layout has two 1,310,720-byte OTA slots; that is a Doser reference, not a Core limit.

Content-Length is the full multipart body. Require firmwareSize < content_len <= firmwareSize + 664, with
overflow-safe arithmetic. Derivation for a 70-byte boundary: opening delimiter up to 74 bytes, one part header
block up to 512 bytes, closing delimiter with CRLF up to 78 bytes; total overhead at most 664 bytes. The
parser still verifies the exact frame and exact file byte count. A missing/zero Content-Length or unsupported
chunked request fails before Application Start. Declared gross oversize and firmware size above the available
partition fail 413 without reading the body; close the socket. The optional route-level safety ceiling is the
current Doser partition snapshot plus 664, not a hardcoded platform-wide Core firmware limit.

The v1 raw receive chunk is 1,024 bytes. This bounds the server buffer to one quarter of Update's installed
4,096-byte sector buffer and gives four full writes per sector, while avoiding 2–4 KiB on the 4 KiB HTTPD task
stack. It is a chosen implementation parameter; HIL will measure throughput, wait latency and heap before any
later tuning. The parser retains at most 78 bytes of delimiter/cross-chunk tail (CRLF + two hyphens + 70-byte
boundary + final two hyphens + optional CRLF). A prefix-matching state machine releases bytes to firmware only
after they can no longer form the delimiter. It handles every split of boundary, CRLF, headers and final
delimiter, and treats a boundary-like but incomplete sequence as firmware bytes. A complete delimiter inside
firmware violates the multipart boundary contract and is treated as framing, then rejected if it creates
another part. No firmware or whole multipart body is accumulated in RAM.

Fixed Doser/Core ownership budget: one 1,024-byte transport receive buffer, one 1,024-byte owned bridge slot,
78-byte parser carry, 512-byte header block, 129-byte filename storage, 71-byte boundary storage, and at most
roughly 256 bytes of counters/error metadata: approximately 3,094 bytes before alignment and synchronization;
implementation must report actual sizeof totals and target below 4 KiB of additional fixed ownership. Keep
large buffers as composition-owned members, not HTTPD stack locals. Arduino Update separately allocates its
4,096-byte sector buffer and ESP-IDF/Arduino may allocate internally; this design does not claim globally zero
heap.

## Application ownership, bridge and state machine

The Doser-local DoserOtaApplication owns the Update session in the main loop. Its narrow authority exposes
nowMs(), availableFirmwareSpace(), beginFirmwareUpdate(size), writeFirmware(bytes,length),
endFirmwareUpdate(), abortFirmwareUpdate(), firmwareError(), stopPumps(), and setOtaInProgress(bool). It has
no Web server, HTTP request, Wi-Fi/MQTT manager or whole Application reference. A separate narrow restart hook
schedules through the existing DoserWebApplication restart owner; there is one restartPending/restartAt pair.
The existing restart route keeps its F9.7F2 duplicate/deadline semantics outside OTA. OTA Start is rejected
with 409 while any restart is already pending; while OTA is admitted or awaiting restart, a new Web restart
request is rejected with 409 and cannot advance the timer. This requires a small product restart-admission
guard in F9.7G, without changing the shared Core bridge. OTA success may arm restart only after successful
finalization, never during data receipt.

The bridge choice is a dedicated single-slot StreamingUploadBridge, not WebApplicationBridge or an ad hoc
unsynchronized mailbox. WebApplicationBridge deliberately retains accepted work after waiter timeout for
ordinary actions; a late accepted OTA chunk must instead cancel its session. The new bridge owns exactly one
1,024-byte chunk copy plus operation metadata, result, generation and cancellation flag under a static
mutex/completion primitive. HTTPD submits one operation, waits for Application completion, then reads/submits
the next bytes. There is no pipeline, dropped chunk or unbounded queue. Application processes at most one OTA
operation per loop at a deterministic point before restart-bridge processing and MQTT. Session admission is
exclusive. A second request that reaches the handler while an upload is active receives 409; while the one
HTTPD task is blocked on a stream, a second connection may merely wait or time out before reaching its
handler. No immediate concurrent 409 is promised.

States: Idle accepts Start only; Starting performs safety/Update.begin; Receiving accepts nonempty bounded
Chunk, End at exact count, or Abort; Finalizing executes Update.end(true); SucceededAwaitingRestart rejects
new Start/Chunk/End and keeps scheduler OTA protection; Aborting performs cleanup; Failed returns to Idle only
after cleanup is complete. Invalid operations return an explicit failure without changing the active session.
Every admitted session gets a nonzero uint64_t generation; chunk/end/abort/cancel carry that value, not a
request pointer. Generation increments monotonically and is never reused during a boot. On UINT64_MAX, reject
new sessions until reboot rather than wrap. A late token cannot act on a later session.

The bridge has Queued, Processing and Completed operation states plus a cancel intent tied to generation. The
HTTPD waiter may time out after acceptance. On START/CHUNK timeout it marks that generation canceled under the
mutex, detaches, closes the HTTP request and never submits END. Application checks cancellation before each
queued operation and after in-progress Start/Chunk work, aborts an active Update, clears the scheduler flag,
then releases the session. A queued stale chunk/END is discarded. An END already executing is special: if
Update.end(true) succeeds, boot selection is committed and restart proceeds even if the waiter timed out; if
END fails, normal cleanup applies. A new Start is rejected until cleanup or the committed restart.
Cancellation cannot interrupt a flash call mid-execution; cleanup is guaranteed when that call returns and the
Application loop continues. ABORT handoff failure uses the same persistent cancellation flag, so cleanup does
not depend on a free command slot. Repeated abort of the same generation is harmless; wrong-generation abort
is rejected.

START sequence: authenticate; validate headers, body bound, filename and first part framing; check exclusive
admission and capacity snapshot; submit Start; in Application, recheck partition capacity, stopPumps(),
setOtaInProgress(true), then Update.begin(size,U_FLASH); enter Receiving only on success. Begin failure calls
abort/reset if needed, clears scheduler OTA flag, leaves pumps stopped, schedules no restart and returns a
bounded 500 result.

CHUNK sequence: require matching generation and Receiving, length 1–1,024, overflow-safe cumulative sum <=
expected file size, then require Update.write() to return the exact length. Short write or error aborts
Update, clears scheduler OTA flag, leaves pumps stopped, schedules no restart and returns 500. Parser sends no
zero-length firmware chunks.

END sequence: require matching generation, complete closing delimiter, no trailing bytes and
receivedFirmwareBytes == expectedFirmwareSize; then call Update.end(true). Failure aborts/cleans up and does
not restart. Success enters SucceededAwaitingRestart and leaves scheduler OTA protection enabled. No further
chunks are accepted.

ABORT sequence: for an active matching generation, request cancel; Application calls Update.abort() if begin
succeeded, clears counters and scheduler OTA flag, leaves pump outputs stopped, and does not schedule restart.
Normal scheduler operation may resume after failure, matching legacy behavior; the pumps are not reactivated
by cleanup itself. A new session is possible only after cleanup.

The response handoff is two-phase. After successful END, HTTPD sends the exact 200 text/plain success body.
Once httpd_resp_send() or the response writer completes, HTTPD submits a no-body ArmRestart acknowledgement;
Application invokes the existing DoserWebApplication scheduler and starts the 1,000 ms delay. If the response
send/ack is lost, a 30-second Application watchdog from successful END arms that same restart once, because
the inactive firmware is already selected and unsafe rollback is not attempted. Thus a normal response is sent
before restart; browser receipt cannot be guaranteed. An END wait timeout is transport-outcome-unknown: END
may finish and restart through the acknowledgement/watchdog path, or cancellation may win before finalization.
No second END or automatic retry is sent.

Default HTTPD recv/send socket timeouts stay 5 seconds; F9.7G does not globally change them. One zero/negative
receive, including HTTPD_SOCK_ERR_TIMEOUT, is terminal: cancel/abort and close the socket, with a best-effort
400 or 408 response if safe. Do not retry forever. The upload also has a provisional 10-minute monotonic
deadline from handler entry, checked between reads and operations; expiration cancels/aborts and closes. Each
accepted Application START/CHUNK/END/ABORT wait is provisionally 30 seconds; this accommodates synchronous
flash work better than the restart route's 1,000 ms wait. These explicit values are implementation choices to
be measured in F9.7G HIL; they are not platform-wide standards. A timeout does not free a Processing slot or
allow a new generation until cleanup. For START/CHUNK/ABORT timeout, send a best-effort 503 and close; after
END timeout, send a best-effort 503 with bounded “Update outcome unknown” text and close, while the
committed-or-canceled generation resolves in Application. Never report 200 without confirmed END success.

Error representation is a fixed enum plus at most 96 bytes of sanitized diagnostic text copied from
firmwareError() while still in the Application context. No dynamic String is required in new OTA state and no
secrets are logged. Mapping: 400 invalid metadata/multipart/size syntax; 401 missing or wrong Basic Auth with
the existing challenge; 409 active upload; 413 declared size/body over bound; 408 idle receive timeout when a
response is still possible; 500 Update begin/write/end failure or internal parser impossibility; 503 disabled
admin or unavailable synchronization/bridge. An error after accepted Start always triggers cleanup; malformed
requests before Start cause no OTA effect. On a large unauthorized or malformed request, 401/400 is best
effort and the socket closes with unread body; zero OTA side effects is the firm guarantee. GET /update and
POST /update return 503 without challenge when WEB_PASS is null, empty or CHANGE_ME_BEFORE_USE.

## Production composition and operation

Final main.cpp composition: one EspIdfWebTransport exposed as normal and streaming capabilities; one
NativeWebService; Application-owned published system/diagnostics snapshots and their synchronizers/publisher;
DoserManagerDiagnosticsFacts and DoserDiagnosticsProjectionSource; F9.7F2 DoserNativeWebRoutes, DoserWebBridge
and DoserWebApplication with its restart authority; one dedicated upload bridge/synchronizer;
DoserOtaApplication; one Doser-local parser/native update route; unchanged product pump, scheduler, Wi-Fi,
time, diagnostics and MQTT managers. Share a synchronizer only where slot lifetime and ownership remain
explicit. Legacy WebManager and broad DoserWebRuntime remain for legacy regression tests but leave production
composition. Do not call their serviceDuringUpload() from HTTPD.

Startup order: initialize safe pump GPIO first; initialize
time/network/storage/pumps/scheduler/diagnostics/MQTT; prepare OTA and restart bridges and their authorities;
publish initial system and diagnostics projections; register Core, Doser normal and streaming routes; then
begin the one NativeWebService on port 80. No admin callback becomes active before pump safety and bridge
readiness. If Web begin fails, continue the autonomous pump/scheduler/time/MQTT loop; OTA is unavailable.

Main-loop order: pumpDriver.loop(), wifiManager.loop(), timeManager.loop(), upload Application
processOne/cancellation service, restart bridge processOne, schedulerManager.loop(),
diagnosticsManager.loop(), publish projections, conditional MQTT service,
DoserWebApplication.serviceRestart(), existing status/yield. From OTA session admission through cleanup or
restart, skip MqttManager::loop() entirely while leaving the existing network connection untouched:
connectIfNeeded() calls synchronous PubSubClient::connect(), and the current MQTT restart command invokes
ESP.restart() directly from its callback. Skipping this loop prevents both blocking reconnect and a broker
command from restarting during flash writes. A connected broker may time out during a long upload; resume the
ordinary MQTT loop after failure/abort, or reconnect after successful reboot. MQTT is not an OTA dependency.
The scheduler OTA flag blocks automatic dosing, and pump outputs were stopped before Update.begin. No
webService.update() remains.

One HTTPD task may be occupied by upload/bridge waits. Other Web requests can be delayed or unavailable during
upload; accept this as temporary product maintenance behavior on the same listener. Do not add another server.
If GET /update reaches its handler while an upload is active, it returns 409 after authentication; it does
not alter the active session. Basic Auth is checked before Application Start or bridge session creation.
The current native GET /update Basic realm is reused for POST. This does not close platform SEC-101, SYS-107 or Core
Maintenance/MNT-102. Product setOtaInProgress() remains a transitional scheduler guard.

## Verification and release gates for F9.7G

Implementation split:

1. **F9.7G2:** neutral Core streaming capability and ESP-IDF adapter, fixed route registration and focused transport tests.
2. **F9.7G3:** Doser multipart parser, dedicated upload bridge, Application OTA owner, auth/error policy and native semantic tests.
3. **F9.7G4:** one atomic production composition change with build, route inventory, regression review and no legacy listener.
4. **F9.7G5:** bare-board ESP32-S3 OTA HIL, evidence review and checkpoint. No production composition state with two listeners is checkpointed.

Core tests: registration/freeze/collision across route kinds, normal POST unchanged, stream bypass of
1,536-byte normal body, bounded/partial recv, timeout/zero/error, content_len bound, response header lifetime,
handler abort and no raw request leakage. Product parser tests: each relevant boundary/CRLF/header split;
binary payload including NUL and boundary-like prefixes; exact/zero/mismatched file size; missing final
boundary; duplicate/extra part; invalid filename/field/Content-Type; oversize metadata and malformed CRLF.
Application tests: Start/begin failure, chunk/short write/overflow, exact End/end failure, Abort/double Abort,
wrong or stale generation, cleanup on every failure, successful restart scheduling. Cross-task tests:
queued/processing timeout, late chunk/END, cancellation ordering, disconnect, next session admission, and END
success despite lost HTTP response. Cutover tests: exact Core/Doser route inventory, POST /update, one
listener, no GET /api/status, no legacy Web owner or webService.update(), Web failure autonomy,
disabled/missing/wrong/correct auth with no effect before authorization.

Rebuild F9.7G and measure RAM, image and OTA slot headroom. F9.7F2 baseline is RAM 48,588 B, image 1,085,528
B, OTA slot 1,310,720 B, headroom 225,192 B (17.18%). The image must fit the inactive slot; no invented
percentage margin is a pass criterion. Confirm that the streaming ownership is fixed-size and measure free
heap before/during/after upload.

Bare-board HIL is safe because no pumps or output stages are attached, but it cannot validate physical pump
output, flow or dose. Back up NVS (0x9000/0x5000) outside the repository without revealing its contents.
Exercise GET /update authentication; unauthorized POST; invalid/oversize size, bad filename, malformed
multipart, mid-upload disconnect, practical partial-upload timeout and a successful full upload. Prove the new
image booted using pre/post otadata or active/running partition offset plus image hash and serial boot
evidence; a temporary build marker, if needed, must be reproducible and excluded from checkpoint unless
deliberately versioned. Measure upload duration, throughput, chunk wait latency, free heap, 200 response
completion, response-to-reset delay, active OTA slot, STA/Web recovery and NVS retention. Compare read-only
NVS hashes and sanitized configuration evidence; a changed hash requires investigation and is not automatic
evidence of config loss. After reboot, exercise GET /, /api/system, /api/diagnostics and authenticated
/update; optionally one safe restart request. Run at least 300 seconds of Web/HTTP soak and check for panic,
watchdog, brownout, reset, heap collapse and socket leakage.

After failed upload the old valid firmware remains selected, scheduler OTA guard clears, no restart is
scheduled, Web remains usable without reboot where the connection allows, and a fresh upload can start after
cleanup. Power-loss at every write/finalization point is not fully proven by this HIL. Arduino Update writes
the inactive partition and changes boot selection only at successful end, which supports the expected recovery
model; Phase 11 may add stronger recovery and validation. F9.7G does not introduce signing, manifests,
device-model checks, downgrade policy or cryptographic firmware verification. Reusable neutral streaming
transport may remain Core; Doser multipart/Update/restart workflow is transitional product code. Shared OTA,
backup, restore, factory reset and common UI/maintenance policy belong to Phase 11/MNT-102.

## Decision

**F9.7G IMPLEMENTABLE — architecture resolved.** F9.7G2 is the current Core streaming HTTP foundation; F9.7G3 is required next. Physical OTA HIL and image/latency/heap measurements remain required release evidence.
