# F9.1 — Web + Realtime architecture design gate

**Status:** design ready for review; documentation only. Phase 8 is CLOSED and
Phase 9 is REQUIRED NEXT. WEB-103 is ACCEPTED — TARGET for native ESP-IDF
`esp_http_server` used from Arduino-ESP32 on the tested ESP32-S3 baseline
(Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7). The production backend is still the
Arduino `WebServer` inside `Esp32WebBackend`. F8.1 classified the CURRENT
stack as NOT FEASIBLE WITH CURRENT STACK for WEB-001. F8.2 compiled both candidates;
F8.3A is IDF HIL PASS; F8.3B is ASYNC HIL INCONCLUSIVE, with one observed
teardown panic and no established root cause. The preserved native baseline is
368/368 PASS; F9.1 reruns none of it. Classic ESP32 needs separate HIL before
deployment. See `WEB_TRANSPORT_SPIKE_F8_3C_COMPARISON.md` for evidence limits.

## CURRENT inventory and migration boundary

`WebBackend` exposes GET/POST route registration, a not-found handler, `begin`,
polling `update`, `stop` and running state. `Esp32WebBackend` implements it with
one heap-owned Arduino `WebServer`, bounded route storage and callbacks invoked
by `handleClient()` in the caller's loop context. It can install routes while
running. `WebTypes` supplies callback-scoped request/body/context and response
writer, Basic-auth helpers and upload callbacks. `WebConfig` supplies enabled,
port and navigation mask. No production WS capability exists.

`WebService` borrows its backend and optional `SystemService`/diagnostics. It
owns fixed page/API registration arrays (eight each), validates paths, adds
default routes during `begin()`, then starts the backend; `update()` polls it
and `stop()` closes it. `GET /`, `/assets/aqua.css`, `/api/system` and
`/api/diagnostics` plus 404 are built in. The system and diagnostics handlers
read their services directly and serialize JSON in the Web callback. `HtmlShell`
renders pages and shared CSS; `WebPageProvider` renders page content and
`WebApiProvider` handles a supplied method/path. Providers are borrowed.

Luma's composition root owns one backend and service. `LumaWebApp` registers
pages and `/api/lumasense/status` (GET), `/mode`, `/profile`, `/manual` (POST)
before Web start. GET reads `FirmwareApp` state/config and diagnostics directly;
POST calls `FirmwareApp` command methods in the Web callback. Setup starts Web
after route composition and loop calls `webService.update()`.

Hydro's `HydroSenseApp` owns one backend and service, registers pages and
`/api/hydrosense` (GET), `/api/control` and `/api/settings` (POST), then starts
and polls Web. The GET serializes a product status reference. Control invokes
controllers; settings mutates config and storage from the handler.

Doser's `main.cpp` owns one backend/service and a local `WebManager`. It
registers `/api/restart` (POST), `/update` (GET page and POST multipart OTA)
before `webService.begin()` and polls both service and manager in `loop()`.
`WebManager` checks legacy Basic auth; its restart handler schedules a restart,
while upload callbacks access firmware `Update`, pump/scheduler and other
mutable services through `DoserWebRuntime`. This is CURRENT legacy behavior,
not a safe pattern to run unchanged in HTTPD's server task. No second physical
Web server was found in these three product compositions.

Route families to migrate, preserving existing names only as inventory:

| Family | CURRENT examples | TARGET boundary |
| --- | --- | --- |
| Read/status | `/api/system`, `/api/diagnostics`, `/api/lumasense/status`, `/api/hydrosense` | Typed Application projections |
| Domain action | Luma mode/profile/manual, Hydro control | Serialized Command path |
| Config | Hydro `/api/settings` | Application Config lifecycle and policy |
| System/recovery | Doser `/api/restart` | System command/recovery safe point |
| Upload/OTA | Doser `/update` POST | Streaming, bounded, authorized system workflow; detailed OTA policy later |
| Pages/assets | `/`, `/assets/aqua.css`, product pages, Doser `/update` GET | Static/page capability; UI migration can be separate |

The CURRENT single-loop callback context lets legacy handlers assume they can
read or mutate loop-owned services directly. IDF HTTPD callbacks execute in a
server task, so that assumption does not transfer. The audit finds direct
mutable access in product providers and Doser OTA; Phase 9 must migrate each
route family through Application boundaries before switching it to HTTPD.

## Target physical owner and lifecycle

Application/Composition Root owns one concrete transport adapter and its
`httpd_handle_t`. That adapter starts and stops the single HTTPD server,
registers bounded HTTP routes and one WS endpoint on the same listener/port,
and manages bounded technical WS client state. It exposes narrow HTTP
registration/response and Realtime publication capabilities; HTTP-only code
need not depend on WS methods. Domain receives neither capability and knows
no HTTP, WS, JSON, session or Auth handshake. Transport never operates hardware
or invokes Domain command handlers directly.

Composition Root declares routes and their borrowed adapters/providers in a
deterministic, finite table with lifetime through server use. Validate capacity,
duplicates and dependencies before starting; v1 freezes registration before
server start. The composition owner retains the descriptor array, route strings,
handler contexts and providers, without moving or destroying them until all
callbacks have stopped. No runtime plugin
discovery, Domain-created routes, service locator, RTTI or `std::function` is
required by Core/Application contracts. Platform `esp_http_server` internals
may allocate; that does not change the static, bounded contract at this boundary.

Network initialization belongs to `NETWORK_INIT`; optional Web/Realtime setup
belongs to `INTERFACES_INIT` after usable network prerequisites. Startup does
not wait for HTTP/WS clients. An optional transport init failure may contribute
DEGRADED; it does not stop autonomous Domain. If it does, composition must
provide an explicit Web HealthProvider backed by a live owner-held failure
condition that survives report capacity zero/overflow. That provider remains
active through SYS-106 handoff until the Web owner confirms recovery. A
RUNNING + DEGRADED startup without that contribution is a composition failure:
the handoff is rejected; ApplicationRuntime remains the writer and moves live
status to ERROR + FAULT + LOCKED under the existing SYS-106 failure boundary.
It must not silently publish OK or remain a normal permanent writer.
`StartupReport` remains historical.

If Web was already started before a later transition to `ERROR`, it can stay
available subject to recovery policy. If fatal startup stopped before
`INTERFACES_INIT`, normal Web has not necessarily started; Web access then
requires a separately composed, safe recovery-start capability/path, without
re-running ordinary startup participants. Failure before `NETWORK_INIT` gives
no basis to assume network access. Neither ERROR nor Web recovery availability
is automatic under SYS-105. In `MAINTENANCE`, Web/Realtime infrastructure continues outside the
NormalProcessing gate. Command policy still restricts actions in both states.
Recovery projections are published by the explicit system/recovery processing
boundary, independently of the normal Domain loop; unavailable dependencies
produce unavailable views rather than reads from partially initialized Domain.

## Cross-task model choice

| Model | Merits | Costs and risks |
| --- | --- | --- |
| A: bounded request/response bridge for all requests | One Application execution context, easy to reuse live providers | Every GET waits for a queue and response; HTTPD task can accumulate bounded waits, harming latency and recovery diagnostics under load. Borrowed request views need copying. |
| B: published immutable snapshots plus queued actions | GET avoids Application round trips; Domain stays single-writer | Requires explicit publication, synchronization, freshness and lifetime; a naive cache becomes a second mutable state machine. Actions still need result correlation. |
| C: hybrid typed snapshots plus serialized command queue | Fast bounded GET, serialized mutations, suitable for ERROR/Maintenance projections and existing typed providers | Needs both publication policy and a bounded response bridge for actions; migration of direct legacy providers is explicit work. |

**Selected TARGET: C.** Domain retains authoritative ownership of its own state;
the system runtime/coordinator retains the SYS-106 system-state writer rules.
Application serializes access and derives separate, typed resource projections for system,
diagnostics, domain status and allowed Config views. Publication produces
immutable-to-transport values: HTTPD either owns a bounded snapshot copy for
the complete serialization or holds a synchronized immutable publication lease
whose slot cannot be reclaimed until that serialization finishes. The producer
never mutates a buffer while HTTPD reads it; an unprotected double buffer is
insufficient if a writer can reuse a slot while a reader still holds it. No
pointer/reference can outlive its owned copy or lease. Publication has bounded
storage, a coherent version/availability marker and explicit lifetime. A
published view may briefly lag its authority, but cannot control Domain or
become another mutable state machine.
No transport-owned Domain state machine or giant universal state object is
created. Domain `DiagnosticProvider<Snapshot>` and `DiagnosticRegistry<Entry>`
remain typed, borrowed and Application-side: Application samples them in its
serialized context, then publishes allowed projections. Unavailable data is
represented explicitly, not silently treated as a valid stale state. The
exact publication primitive and capacities are implementation gates.

For GET, HTTPD validates request/access, takes a safe immutable projection,
then the Application/Web projection adapter serializes a bounded response. A
read does not call mutable Domain internals from HTTPD. HTTP full typed/resource
snapshots, not WS, are the client's recovery source. Several snapshot resources
avoid an obligatory all-device object. A consistent resync requires a published
runtime identity and stream watermark across the resources read. Application
binds each complete projection version to a position N in the same serialized
publication order as client-visible notifications; a multi-resource resync must
obtain a coherent set or detect a version change and retry. The concrete wire
fields and publication primitive remain WEB-102/RT-101 work.

For POST/action, HTTPD bounds and decodes transport input, performs transport
validation and future Auth/authorization, copies an owned typed request into a
bounded Application queue, and waits at most a bounded time for a response.
The queue owns the validated, bounded command value; it never retains callback-
scoped `WebRequest`, path, header, body or HTTPD request pointers. Enqueue-full
is explicit. Application serially executes the existing `CommandPipeline`
validator, policy, Safety/Action Locks gate and Domain handler in that order.
System/recovery/Config actions
use their own narrow authorized Application paths; they do not get a shortcut
to mutable services. The command may return `Completed`, `Rejected`,
`InvalidState` or `OperationStarted`. Application preserves that semantic result;
an Application/Web adapter maps it to HTTP representation. Exact HTTP codes,
error envelope, correlation and idempotency remain open. `OperationStarted`
returns start/acceptance without keeping HTTP open until operation completion;
later state may be read through HTTP status snapshots and WS notifications.
F9.1 does not invent operation IDs.

Queue full, response timeout or projection unavailable yields a bounded
transport-level failure response. A timeout does not imply that an accepted
action was cancelled: the bridge must distinguish rejection-before-enqueue
from accepted-but-outcome-unknown. A response uses bounded bridge-owned stable
storage, such as a fixed slot/mailbox with generation and explicit ownership;
it never points to the HTTPD callback stack/request. On timeout the waiter
abandons its claim. A later Application completion can discard the result or
publish it only to a still-valid matching slot; it cannot write a freed or
reused slot. The accepted command may still execute after HTTPD returns.
Retries of non-idempotent actions need a later correlation/idempotency contract.
These transport failures do not set Core ERROR, Safety LOCKED or request a
restart, and they do not stop autonomous Domain processing. HTTPD callback
work is limited to bounded parse, validation, enqueue, safe projection access
and bounded serialization; it performs no long Domain operation, hardware I/O
or unbounded wait.

## Realtime, resync and publication

V1 WS primarily sends server-to-client notifications. Incoming frames serve
protocol control/heartbeat and possible later Auth/session control; Domain
commands use HTTP. Each v1 client-visible notification belongs to one
Application-owned, per-runtime Realtime stream with its own contiguous stream
sequence and `RuntimeIdentity`. Application assigns the stream position after
filtering/synthesis, in the same serialized boundary as the published snapshot
watermark. Relevant state changes must produce a notification/position or a
resync-required signal. `EventMetadata`/`EventSequence` may be retained as
source-event metadata but are not automatically the Realtime sequence: filtered
or synthetic notifications would make raw Domain event gaps ambiguous. A
different filtered subscription needs its own contiguous sequence or an explicit
mapping before clients can use gap detection. The transport preserves order for
an individual WS stream. No global ordering across HTTP, WS and future MQTT
is promised.

On connect/reconnect, establish live WS subscription first and capture its
runtime/stream position S. Then obtain full HTTP snapshot(s) carrying/logically
bound to `RuntimeIdentity` and stream watermark N. Require matching identity
and N >= S; a lagging published projection with N < S must be refreshed/retried
or reported unavailable, never accepted as a complete resync. Buffer a bounded
overlap while fetching; discard notifications at or below N and apply those
above N in contiguous order. Thus a change
between subscription and GET is either included in the snapshot or present in
the buffered stream. If overlap overflows, identity changes, sequence continuity
fails or a coherent multi-resource snapshot cannot be obtained, do another full
HTTP resync; never silently continue from a known gap. Starting GET before
subscription is not a valid no-replay flow. There is no history replay or
durable event-log requirement. Exact wire fields remain RT-101 work.

The transport stores only bounded socket/client IDs, connection state, queue
occupancy and future session data. It owns no Domain mode or authoritative
device state. No unbounded queue is allowed; a slow client cannot stall Domain
or all other clients. Drop/disconnect/resync is allowed because HTTP snapshot
is the recovery source. If a notification is dropped, the owner must make the
loss detectable through a later stream gap, an explicit resync-required signal
or disconnect; a silent final drop with no later notification is invalid.
Numeric capacities, thresholds, heartbeat and exact backpressure policy
require later measurement. Application publication from
outside HTTPD uses a legal server work queue/async WS send boundary and checks
ownership, callback context and client lifetime; no raw send from an arbitrary
task. The selected server's task context is a design constraint, not an
implementation in F9.1.

## Access, payload and serialization

Route registration does not grant public access. Leave a composed boundary
`request/session -> Auth -> authorization/policy -> read or command` for
SEC-101 and WEB-101; neither is decided here. Projections and notifications
must explicitly select visible fields. Passwords, tokens, private keys and
MQTT secrets never appear automatically, consistent with SEC-002. Each endpoint
defines a bounded body policy before large allocation where technically
possible. Multipart/upload paths stream with their own Config/Restore/OTA
limits later; the spike's 256 B fixture limit is not a product standard.
Application/Web projection adapters own JSON/HTTP serialization, never Domain.
No JSON library choice is needed for this decision.

## Existing contracts: KEEP / ADAPT / REWRITE / REMOVE

| Contract | Disposition for TARGET | Reason |
| --- | --- | --- |
| `WebService` | ADAPT semantics; REWRITE direct handler execution | Keep one service's lifecycle, bounded explicit composition and common routes. Move reads/actions behind Application projections/queue; current callbacks and `update()` polling do not define IDF task policy. |
| `WebBackend` | ADAPT or replace narrow HTTP capability | Its HTTP registration/response pieces can inform a new adapter; `update()` is Arduino polling-specific. Adding WS methods would make one broad interface. Keep WS publication separate. |
| `Esp32WebBackend` | KEEP as CURRENT legacy until migration; REMOVE from target composition | It owns Arduino `WebServer`, so cannot implement WEB-103 target by renaming. New IDF concrete owner is required. |
| `WebTypes`/`WebConfig` | ADAPT | Callback-scoped request/body and upload views cannot cross tasks by borrowing; copy bounded data and revise config/lifecycle as needed. |
| `WebApiProvider` | ADAPT registration idea; REWRITE direct service access | Keep small product route descriptors/adapters, but provider callbacks must use projections or serialized action path. |
| `WebPageProvider`/`HtmlShell` | KEEP CURRENT; ADAPT later | Static pages/assets can remain during transport migration; UI redesign is outside F9.1. Any dynamic page read must use a safe projection and cannot access mutable Domain state directly from HTTPD. |

## Decision status and implementation sequence

WEB-102 and RT-101 become **PARTIALLY ACCEPTED — TARGET** for the foundations
above. Their endpoint schema, wire envelope and operational limits remain open.
WEB-101 and SEC-101 remain **DECISION REQUIRED**; so do public/Auth policy,
SEC-002 detail, final payload/backpressure and compatible legacy-route mapping.
WEB-103 remains ACCEPTED — TARGET. No production migration is claimed.

1. **F9.2:** IDF production backend foundation, one physical owner/port,
   bounded deterministic HTTP+WS registration and lifecycle; verify target
   hardware constraints separately.
2. **F9.3:** typed published read projections, safe HTTP snapshot path,
   visibility and unavailable behavior.
3. **F9.4:** owned bounded action queue, response timeout/lifetime, serialized
   Command and system/recovery policy bridge.
4. **F9.5:** WS publication capability, task-safe send, runtime identity and
   sequence metadata.
5. **F9.6:** reconnect/resync binding and measured slow-client/backpressure
   baseline; resolve remaining RT-101 wire and limit gates.
6. **F9.7:** migrate legacy Web route families and audit Luma/Hydro/Doser
   composition, including special Config/OTA/restart handling and ERROR/
   MAINTENANCE access.
7. **F9.8:** integration and Phase 9 closure audit after required validation.

This split is a plan, not evidence that any Phase 9 production capability has
already been implemented.
