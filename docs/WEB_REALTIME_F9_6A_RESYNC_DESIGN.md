# F9.6A — Snapshot watermark, resync and backpressure design gate

**Status:** design ready for review; TARGET only. F9.6A changes documentation,
not the F9.2–F9.5 runtime. Baseline: F9.5 checkpoint `265e2f6`, native
391/391 PASS, ESP32 F9.5 compile-only test build PASS, F9.5 S3/HIL NOT RUN.
The numerical limits below describe F9.5 implementation, not platform limits.

## One position and its meaning

`RealtimeStreamSequence` is the sole numbered position of the per-runtime,
client-visible stream. It is distinct from `Events::EventSequence`, a request
ID, a durable event ID and a snapshot revision. A snapshot watermark is a
*position in that stream*: a coherent full snapshot at N contains every
authoritative change relevant to that subscribed stream through N, inclusive.
Every change to a resource in the resync set either gets a stream position and
notification or makes the stream require resync. An unchanged resource is
restamped with the current position when publishing a new coherent cohort.
Filtered subscriptions need a separately defined contiguous stream or mapping;
raw EventSequence gaps cannot be interpreted as Realtime gaps.

There is a legal position before the first notification. One neutral
`RealtimeStreamPosition` value is used everywhere: as the snapshot watermark,
the `StreamStart` current position and the result of `currentPosition()`. It is
either `BeforeFirst` or
`At(RealtimeStreamSequence)`. `BeforeFirst` is ordered before every legal
sequence; it is not a legal sequence zero. `RuntimeIdentity` remains mandatory
in either case. The first notification after `BeforeFirst` has sequence 1.
The Application-owned sequencer needs a read-only `currentPosition()` query
that does not issue a number. Only the serialized Application owner calls that
query. HTTPD never reads the mutable sequencer directly: Application hands the
transport an immutable position copy in subscription control, and publishes
the position inside the same synchronized bounded-copy values used for HTTP
snapshots. The transport never advances or resets the sequencer.
`UINT64_MAX` is the last legal issued number; subsequent issue returns
`Exhausted` without wrap. Exhaustion disables normal Realtime publication and
drives clients to disconnect/resync. HTTP may still serve the last coherent
snapshot; if relevant authority changes without a new position, that resource
must be unavailable as a *current* snapshot until a new runtime or explicit
recovery. There is no automatic Core restart, Health or Safety transition.

## Snapshot and publication order

Each full typed resource projection carries a bounded, transport-neutral
wrapper: `{resource value, RuntimeIdentity, RealtimeStreamPosition watermark}`
plus explicit
availability/coherence state. The wrapper is copied atomically with its value
through the publication boundary. `PublishedSnapshot<T>` currently copies one
trivially copyable value per resource under a synchronizer; it has no
multi-resource transaction. HTTP serializers may later place logical metadata
in an envelope or headers, but the foundation does not choose JSON names,
binary encoding or Domain payload schema. An unavailable resource is never
reported as a current full snapshot.

Application/Composition explicitly declares the finite members of the full
resync publication set. Membership is a typed composition property, not a URL
pattern: dynamic system/status/domain resources may be members; static HTML,
assets and unrelated endpoints are not. For every issued Realtime sequence N,
Application rebuilds or restamps every member with N, including members whose
payload did not change. This is the selected global coherent publication-set
model; no `SnapshotEpoch` is added.

One serialized Application owner performs a relevant transition in this order:

1. Enter the publication barrier and apply/observe the authoritative change.
2. Issue N once; N is never reused, even if a later step fails.
3. Build complete, validated projections of every resource in the resync set
   from the same authoritative cut, stamping all of them with N. For startup,
   publish the complete baseline with `BeforeFirst` before opening subscription.
4. Publish all wrappers. Keep the HTTP resync cohort unavailable while any
   member is incomplete; expose it as coherent only after all members succeed.
5. Submit the normal WS notification carrying N. Leaving the barrier lets the
   next relevant transition begin.

This chooses snapshot-before-WS over WS-before-snapshot: if WS submission N
fails, HTTP already contains N. The barrier is an Application serialization
and publication gate, not a second state owner. An HTTP reader racing the
barrier must copy its complete resource value and metadata and verify the
cohort is still valid. A single independent F9.3 `publish()` call does not
provide that gate by itself. Failure to build or publish any resource keeps
the cohort unavailable, suppresses normal WS N and enters resync-required
state. Application retries a complete coherent cohort from current authority
before reopening the stream; it does not present a partially updated set as
current. If no later semantic state change occurs, an explicit recovery
operation republishes current authoritative projections at the already-issued
current position N. That recovery publication does not issue a new sequence.
If a later change issues N+1 and publishes a complete cohort, that N+1 cohort
also repairs recovery; N remains spent. Neither failure changes Core
Health/Safety automatically.

## Connect, snapshot and client algorithm

The server sends a technical `StreamStart` on every new WS connection. A new
client is `CONNECTING` and excluded from normal notification fan-out. In one
serialized
Application subscription turn, capture `{RuntimeIdentity, currentPosition S}`,
accept the marker for this exact connection and arm delivery of notifications
issued *after* S. No new sequence may be issued between capture and successful
marker acceptance. The HTTPD owner changes the client to `LIVE` only after
`StreamStart(S)` has been sent successfully; queued normal work must test this
state, so no normal notification can be visible first. Notifications `<= S`
need not be delivered to that new client because its later HTTP snapshot must
cover S; notifications `> S` must
be sent after `StreamStart` or the connection must be closed. F9.6B must prove
HTTPD work ordering or add a bounded per-connection gate; it cannot silently
skip notifications while the marker is pending. If the marker cannot be queued
or sent, close that connection. `StreamStart` reports the immutable position
copy supplied by Application and consumes no sequence. It must arrive without
waiting for a Domain event. An fd
alone is insufficient for pending-client identity after reuse; a bounded
connection generation or equivalent lifetime token is required.

The client first establishes WS and receives `StreamStart(S)`, then starts
buffering a bounded overlap and fetches all required full HTTP resources. It
accepts a resync cohort only when all resources are available, carry the same
RuntimeIdentity as the live WS connection, have the same watermark N and
`N >= S` under `BeforeFirst < At(1) < ...`. Different watermarks cannot be
combined by taking their maximum: a resource at N may omit a change in the
resource read at N+1. The client retries older resources or restarts the full
GET set until equal, within a bounded client retry policy. Rapid changes can
prevent convergence; that is an explicit resync-unavailable outcome, not
permission to accept mixed data. No giant device object, server replay log or
atomic multi-endpoint HTTP transaction is required. An extra SnapshotEpoch
would duplicate the ordering axis and is not selected for v1.

Once a coherent cohort at N is installed, discard buffered notifications
with sequence `<= N`, then apply only contiguous `N+1`, `N+2`, ... in order.
For `BeforeFirst`, expect 1. A notification at or below the applied position
is an overlap duplicate and is ignored; no history dedup table is needed.
Any sequence greater than expected, RuntimeIdentity change, buffer overflow,
missing `StreamStart`, unavailable HTTP resource, inconsistent cohort or
connection loss returns the client to full resync. An unavailable resource
cannot be replaced with an older snapshot as current authority. Client overlap
capacity and retry timing are client policy, not server history capacity.

The minimal logical wire contract is `Notification(runtime, sequence, kind,
payload)` and `StreamStart(runtime, currentPosition)`. `kind` identifies a
bounded Application/Web projection notification class or resource; it does
not create a Domain-global event taxonomy. Technical control remains separate.
`ResyncRequired` is an internal sticky condition and may have a best-effort
control frame later. Correctness never depends on successfully sending that
frame. Final JSON/binary spelling, route names, Auth/session and heartbeat
remain open.

## Loss, disconnect and backpressure

| Condition | TARGET v1 response |
| --- | --- |
| Snapshot publication fails after N was issued | Suppress normal WS N; mark cohort unavailable and set sticky global `realtimeResyncRequired`; disconnect existing WS clients. |
| Normal `publishRealtime(N)` returns Busy, QueueWorkFailure or another pre-acceptance failure | N remains spent; set sticky global resync and actively disconnect existing WS clients. Never wait for N+1 to reveal the gap. |
| Work is accepted but send fails for one fd | Close only that connection; other clients continue if their sends succeed. If targeted close itself cannot be established, escalate to global transport recovery. |
| Client disconnects before/during send | No global fault; reconnect begins with StreamStart and full HTTP resync. |
| Sequence exhausted | Stop normal notification acceptance; disconnect clients, retain coherent HTTP snapshots only while they remain current. |

Application owns the policy decision that continuity is lost and recovery is
required; Domain never sees this state. Transport owns the technical sticky
disconnect/resync request and its execution. The Application decision sets
that transport request before subsequent normal publication. Required snapshot
publication failure, any post-issue pre-acceptance failure including
`Busy`/`QueueWorkFailure`, and global transport loss that breaks continuity set
it; one-fd send failure does not.

While sticky is active, normal WS fan-out is suppressed and new handshakes
remain `CONNECTING` only long enough to reject/close them; they never become
`LIVE`. Normal Domain processing continues. Application may continue issuing
positions for semantic changes and publishing complete current HTTP cohorts,
so recovery ends at the latest current position rather than the failed one.
A dedicated transport-control attempt must be scheduled immediately,
independently of any
future Domain event, to enumerate and close all active WS sessions. Retry a
failed control submission while the transport remains running. This control
path cannot rely only on a free normal publication slot. In ESP-IDF 4.4.7,
`httpd_sess_trigger_close()` itself queues work and can fail; it is not an
unconditional escape from a saturated/broken control queue. Retries are
scheduled and bounded, never a busy loop or a block in Domain processing. If control work
or session close cannot make progress, an independent lifecycle supervisor
must stop/recycle the HTTPD server outside Domain processing. That fallback
also drops HTTP connections; serve coherent HTTP snapshots again after
recovery. Clear sticky state only after all affected old sessions are closed or
invalidated, a complete coherent HTTP cohort exists at the current Application
position, and transport can accept a fresh connection and deliver
`StreamStart`. Merely submitting disconnect work never clears it. If the
snapshot was already coherent and only WS submission failed, no republish is
needed. F9.6B must specify
ownership, lock order, bounded retry/fallback and fd-generation safety before
claiming the disconnect guarantee. F9.6C must exercise queue failure and
actual saturation. No silent final gap is allowed in a healthy running server.

`Accepted` in F9.5 means only that an owned work item reached the HTTPD work
queue; it is not per-client delivery or ACK. The current four slots and
256-byte payload are implementation capacities. `Busy` is a semantic loss
requiring recovery, not a prompt merely to increase pool size. A slow client
must not block Application/Domain or allocate unbounded memory. Target policy
is to disconnect a client that breaches a measured send-progress limit.
Because F9.5 sends on the HTTPD task, F9.6C must measure whether a slow client
delays other clients or HTTP. No numeric threshold or latency guarantee is
accepted before that HIL evidence. Without heartbeat, detection of a silent
network blackhole can be delayed; heartbeat necessity and interval remain a
later measured decision.

## F9.6 implementation and evidence gates

**F9.6B:** implement neutral position/wrapper, read-only sequencer position,
Application publication/cohort barrier, StreamStart ordering and client gating,
sticky resync plus dedicated disconnect/fallback lifecycle path. Add focused
native tests for before-first, cohort mismatch, gap, final drop, Busy, per-fd
failure and stop/restart races, and ESP32 compile-only proof. Keep final wire
spelling and product migration outside this step.

**F9.6C:** HIL with one and two WS clients: shared HTTP+WS, StreamStart,
snapshot watermark, contiguous notifications, reconnect/full resync, injected
missing publication and RuntimeIdentity change, actual work-pool saturation
(observe `Busy` or another measured boundary), slow client, one failed client
while another continues, HTTP responsiveness under WS load, network
loss/recovery where reproducible, heap samples and absence of panic, watchdog
or reboot. Measure control-path retry/fallback and reconnect latency. Prefer
the first classic ESP32 HIL of the production transport here if hardware is
available; Phase 8 HIL was on ESP32-S3. Do not assume a board is connected.

**F9.6D:** review the implementation and HIL evidence, revise thresholds or
policy only where measurements require it, then checkpoint in a separate
review step. SEC-101 Auth remains outside F9.6, though connection state and
resync must admit future authenticated sessions. RT-101 remains PARTIALLY
ACCEPTED — TARGET with these logical envelope and recovery rules; final wire,
heartbeat, numerical limits, exact thresholds and Auth/session remain open.
EVT-102 becomes PARTIALLY ACCEPTED — TARGET only for HTTP full snapshots as
recovery authority, runtime identity, Realtime watermark and resync relation;
typed Domain payload schemas remain open.
