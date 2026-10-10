# F10.3C - Home Assistant Realtime client HIL

**Date:** 2026-10-10
**Home Assistant:** 2026.10.0
**Device:** production Luma, API 1.1
**Verdict:** **PASS WITH RESERVATIONS / READY FOR CHECKPOINT**

## Scope

F10.3C adds the bounded Home Assistant WebSocket client, coherent HTTP resync,
notification-triggered refresh, a 60-second safety poll and the existing 10-second
polling fallback beneath the single Luma coordinator. The existing Device Registry
identity, ConfigEntry and 13 read-only entity unique IDs remain unchanged. No command
authority was added.

## Confirmed results

The candidate was installed in a real Home Assistant 2026.10.0 instance and exercised
against the production Luma API 1.1 server. The following results were confirmed:

- one Luma device, exactly 13 available entities and no duplicates;
- the same 13 entity unique IDs before and after installation;
- a valid WebSocket `stream_start` and a coherent HTTP status watermark;
- six notification-driven state updates with Home Assistant Recorder latency from
  71 ms through 603 ms;
- contiguous observed notification sequences for those six updates;
- an ESP restart produced a new `RuntimeIdentity` while preserving `DeviceId`, the
  ConfigEntry, Device and all entity identities;
- all 13 entities became unavailable during the restart and recovered automatically;
- an existing ConfigEntry reload restored one device and 13/13 available entities,
  without duplicates;
- a bounded WebSocket capacity check was consistent with one active HA client; this
  is indirect evidence because internal HA task counts were not inspected;
- a five-minute soak showed no observed protocol error, panic, device restart or
  duplicate entity/device;
- the deterministic Python suite passed 66/66 tests with zero failures, errors or
  skips, and all ten candidate Python files passed `py_compile`;
- the rollback package contains the previous integration files and sanitized registry
  identifiers; the established restore-and-reload procedure was confirmed available.

The six Recorder observations demonstrate notification-driven refresh because their
71-603 ms latency is well below the 10-second fallback interval. HTTP remains the
authoritative state source; notifications are refresh hints and never patch entity
state directly.

## Availability, fallback and lifecycle

Deterministic tests cover failed HTTP refresh, fallback rearming, API capability
gating, downgrade, overlap, gaps, reconnect, unload, rejected unload, shutdown and
cancellation during HTTP and WebSocket work. Changing the coordinator interval does
not publish a cached snapshot as a successful refresh. A real failed HTTP request
therefore keeps `last_update_success` false until a later real HTTP refresh succeeds.

WebSocket attempts require a current successful read with the expected Luma identity,
API major 1/minor at least 1 and valid Realtime metadata. API 1.0, a different major,
missing or malformed metadata, identity mismatch and an unverified stale snapshot all
block WebSocket connection attempts. The fallback remains the F10.2 10-second HTTP
polling path.

## Explicit limits

The following scenarios are not reported as physical PASS:

1. The 60-second safety polling interval is configured and covered by deterministic
   tests, but its actual request cadence was not measured directly inside HA.
2. A WebSocket-only physical failure while HTTP remained available was **NOT RUN**.
3. API downgrade from 1.1 to 1.0 passed deterministic tests but was **NOT RUN** in the
   real HA instance.
4. The HA soak was limited to five minutes and does not prove long-term memory
   stability or absence of a slow leak.
5. If initial setup observes API 1.0 and the device later changes to API 1.1, WebSocket
   activation requires an integration reload. Polling remains available. This is a
   known P3 capability-promotion limitation.
6. Internal HA task counts were not measured directly. The bounded WebSocket capacity
   test is only indirect evidence of one active HA WebSocket client.

These limits are nonblocking for the read-only F10.3C checkpoint. A later validation
may measure passive safety-poll cadence, isolate WebSocket failure while preserving
HTTP, exercise a live API downgrade and run a long memory soak.

## Status

F10.3C is **CLOSED/CURRENT** for the Luma read-only Realtime client. F10.2 polling
compatibility remains available as fallback. Luma commands are not implemented.
F10.4 is **REQUIRED NEXT**.
