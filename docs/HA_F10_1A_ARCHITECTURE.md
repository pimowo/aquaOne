# F10.1A — Home Assistant integration architecture gate

**Date:** 2026-10-09
**Baseline:** `46e7e7220d26ed11476fe0323e5d847957807ac3` (`main == origin/main`)
**Verdict:** **B — SERVER PREREQUISITE REQUIRED.** F10.1B must publish a stable device identity and an explicit API compatibility contract before F10.2 creates HA config entries or entities. This is a design checkpoint only.

**Current checkpoint (F10.1B, 2026-10-09): F10.1A CLOSED; F10.1B CLOSED/CURRENT.** The F10.1A audit and blocker table below record the state at that design gate. Common `GET /api/system` now publishes canonical `device_type` (`luma`, `hydro`, `doser`), `device_id` (12 uppercase hex characters from the ESP32 factory/default base MAC48) and `api_protocol_version` (`major: 1`, `minor: 0`) while retaining legacy fields. Luma's existing read-only status schema is covered by this API major. Physical identity and reboot stability passed on classic ESP32 Luma and ESP32-S3 Doser; the Luma test completed 9/9 cases and 13 stable system samples over 62.66 s. AP/STA states were not exhaustively toggled.

Phase 9 is CLOSED; Phase 10 is IN PROGRESS. **F10.2 Luma read-only integration is REQUIRED NEXT**; the HA custom integration is not implemented yet. Product WS, `/api/capabilities` and mDNS/Zeroconf remain future work. SEC-101 remains OPEN, and Doser MQTT/Discovery remains LEGACY CURRENT until F10.6.

## Baseline and authority

Phase 0–9 are closed by [F9.8B](WEB_F9_8B_PHASE9_CLOSURE.md). Core native HTTP and optional Realtime/resync are CURRENT. Luma, Hydro and Doser have production native HTTP; none has production product WS, a `RealtimeSnapshot` cohort or product notifications. Doser has local streaming OTA and retains MQTT/Discovery as LEGACY CURRENT. Phase 10 starts with this architecture gate; no HA integration or shared Python client exists yet. Phase 11 remains FUTURE.

One custom integration, domain `aquaone`, serves Luma, Hydro and Doser through one transport client and separate product adapters. Gas, Clima and Fauna may be added later. The device remains autonomous: restarting HA, losing LAN, losing a router or removing the integration cannot alter its Domain processing or safety. HTTP GET coherent snapshots are authoritative; HTTP POST uses the device's Application command and policy path. Future WS only reports changes and prompts refresh/resync. No Domain commands use WS v1. A successful POST transport write does not prove the action took effect; the semantic response and later snapshot determine that. Unknown outcomes must not cause automatic retry of non-idempotent actions.

## Current API audit

| Surface | CURRENT evidence | F10 implication |
| --- | --- | --- |
| `GET /api/system` | `NativeWebService::handleSystem` returns `deviceType`, `deviceName`, `firmwareVersion`, `hardwareVariant`, `aquaCoreVersion`, `uptimeMs`, `restartReason`. | Firmware/Core versions exist. `deviceName`, host and IP are unsuitable unique IDs. No stable `device_id` or `api_protocol_version`. |
| Core identity | Legacy `AquaCore::DeviceIdentity` contains type/name/firmware/hardware fields. CURRENT `Identity::DeviceIdentity` contains type only. IDN-101 accepts a future pair `(device_type, device_id)` with a full factory/base MAC48 DeviceId. | IDN-101 is an accepted target contract, **not a CURRENT public value**. Do not derive ID from active interface MAC, MAC6, runtime identity, name or IP. |
| Product type | Luma production legacy type is `lighting-controller`; Doser uses `dosing-controller`. IDN-101 canonical product tokens are `luma`, `hydro`, `doser`. | Current type strings cannot silently be treated as canonical adapter keys. F10.1B must publish an explicit, stable canonical `device_type`. No endpoint probing or hostname heuristic. |
| Luma `GET /api/lumasense/status` | Current native response has `mode`, `activeProfile`, `activeProfileName`, `dayState`, `localTime`, eight `requestedLevels`, eight `finalLevels`, `globalPowerLimit`, `timeValid`, `wifi` (`state`, `connected`, `ip`, `rssi`) and `overallHealth`. A read failure returns 503. | Enough values for a small read-only adapter after identity/version prerequisite. There is no watermark or WS continuity metadata. |
| Hydro/Doser | Hydro has native status/settings/diagnostics and actions; Doser has native HTTP and streaming OTA. | Neither is an F10.2 adapter. Doser's current MQTT entities remain in service through F10.6. |

Source: [Core system projection](../aquaOneCore/include/AquaCore/Web/CoreWebProjections.h), [Core native serializer](../aquaOneCore/src/Web/NativeWebService.cpp), [legacy identity](../aquaOneCore/include/AquaCore/System/DeviceIdentity.h), [CURRENT neutral identity](../aquaOneCore/include/AquaCore/System/Identity.h), [IDN-101](ARCHITECTURE_VNEXT_DECISIONS.md), [Luma native serializer](../aquaOneLuma/src/web/LumaNativeWeb.cpp), and [Phase-9 audit](WEB_F9_8A_PHASE9_CLOSURE_AUDIT.md). No source or runtime test was changed or run at this gate.

## Package and ownership

Place the integration in this repository at `homeassistant/custom_components/aquaone/`. This keeps firmware/API and adapter changes reviewable together while allowing the `custom_components/aquaone` directory to be copied for manual installation or packaged for a later HACS release. A separate repository is possible after API stability, but would require synchronized cross-repo changes now. F10.1A creates no package.

Initial package, created only when implementation begins:

```text
homeassistant/custom_components/aquaone/
  __init__.py             # config entry setup/unload, coordinator ownership
  manifest.json           # domain, config flow, version, future discovery matcher
  config_flow.py          # user flow; zeroconf/reauth only when available
  const.py
  coordinator.py          # one polling snapshot owner per config entry
  api/client.py           # shared async HTTP transport and common endpoints
  api/models.py           # validated common response models
  api/errors.py           # typed client failures
  products/base.py        # product adapter contract
  products/luma.py        # Luma status validation and semantic mapping
  sensor.py               # first read-only platform
```

Add `binary_sensor.py` only for a useful Boolean in F10.2, and `switch.py`, `light.py`, `number.py`, `select.py`, `button.py`, `products/hydro.py` and `products/doser.py` only when their feature gate arrives. Keep transport JSON parsing in the API layer, product meaning in adapters, and HA entity lifecycle in platforms. Entities consume typed adapter output from the coordinator; they never perform independent HTTP requests. One config entry represents one physical device and contributes one HA Device.

`AquaOneClient` is async and uses the HA supplied HTTP session. It accepts host/IP and optional port, applies an implementation-level timeout, bounds response bytes before JSON decoding, validates content type/schema, and distinguishes HTTP and semantic failures. Its common operations read `/api/system` and `/api/diagnostics`; product GET/POST paths remain adapter-owned arguments or narrow methods. No WS dependency is required for F10.1/F10.2. Client errors: `ConnectionError`, `TimeoutError`, `InvalidResponseError`, `UnsupportedProtocolError`, `AuthenticationError`, `DeviceRejectedError` (under an aquaOne-specific base). Avoid relying on bare generic exceptions. HTTP 401/403 maps to auth failure, malformed or oversized JSON to invalid response, unsupported major version to unsupported protocol, and a valid rejected action to device rejection. A timeout after POST is an unknown outcome. Timeout, polling interval and retry/backoff values are provisional client settings to measure, not device protocol constants. Bound concurrency and avoid overlapping poll cycles or immediate retry loops that flood ESP.

Use HA's bundled async HTTP support and coordinator helpers, plus Python standard library. Do not add an external Python package without a concrete need. Keep pure response validation and adapter mapping testable without a running HA instance.

## Stable identity, compatibility and discovery

F10.1B should extend the common `GET /api/system` response with **required** `device_id` and canonical `device_type`, backed by the accepted IDN-101 source and grammar. `device_id` is the 12 uppercase hex representation of the full factory/base MAC48 selected by IDN-101, not the compact MAC6 or a network-interface address guessed by HA. This is a public technical identifier, not an auth credential. The full `(device_type, device_id)` pair is the stable integration identity. HA config entry unique ID and Device Registry identifier use a deterministic namespaced encoding of that pair, for example `luma:246F28A1B2C3` scoped to `aquaone`; entity unique IDs append a stable entity key, for example `luma:246F28A1B2C3:mode`. The delimiter/serialization must be frozen and migration-tested before shipping F10.2. Never use IP, host, friendly name, profile name, runtime identity or runtime array ordering. Channel keys may use the documented fixed physical channel number (1–8), not position in an unordered response. Same hardware reflashed as another product changes the pair; policy for preserving HA registry history across that transition is future work.

F10.1B should also expose required `api_protocol_version` on `/api/system` as a public, product-neutral compatibility field. Select a documented major/minor representation and initial value during F10.1B; this gate does **not** assert a number already exists. The client rejects unknown major versions and invalid/missing fields; compatible minor additions are handled by the product adapter. Version the Luma status schema explicitly in the F10.1B contract or document its compatibility under the same API major before F10.2. Keep firmware version, Core version, API protocol version and HA integration version independent. `firmwareVersion` and `aquaCoreVersion` are already available; the HA manifest version is a separate Python release version.

F10.2 uses a static capability set per known product adapter and supported API version. Putting many flags in `/api/system` would expand the common contract without a current dynamic need. `GET /api/capabilities` is a FUTURE option if product variants create a real need for dynamic entity discovery; it is not an F10.2 blocker.

Config Flow starts with manual host/IP, optional nondefault port, and credentials only if a required endpoint uses them. It fetches `/api/system`, validates identity, product type and protocol version, then creates the entry or reports unreachable, invalid device, unsupported protocol or auth failure distinctly. Unsupported products are rejected rather than misclassified. Set the stable config flow unique ID and abort duplicates. A future Zeroconf flow for candidate `_aquaone._tcp.local.` first connects to the announced host/port and verifies the same server identity; TXT may carry canonical type, device ID and protocol version as hints, but untrusted discovery data cannot grant command authority or replace the verified HTTP response. Only matching verified identity can update an existing entry's host/port after DHCP change. Do not declare a Zeroconf manifest matcher until firmware publishes an agreed service and TXT schema. Manual setup works without mDNS. Reauth is added if credentials become necessary; credentials must not appear in logs or diagnostics. SEC-101 remains OPEN. Doser Basic Auth currently guards admin routes; public read APIs on Luma/Hydro are CURRENT behavior, not a permanent security policy.

Device Registry: manufacturer `aquaOne` only when that branding is accepted for the integration; model comes from verified canonical `device_type` (with a display mapping), software version from `firmwareVersion`. `aquaCoreVersion` may be diagnostic metadata. Use a configuration URL only from the validated configured host/port and actual Web listener, never invented cloud data; with future auth or HTTPS decisions it may be omitted. One physical aquaOne product produces one HA Device and its entities attach to that device. Home Assistant's [Config Flow unique ID rules](https://developers.home-assistant.io/docs/core/integration/config_flow/) reject IP and mutable names and support host updates after rediscovery; its [Device Registry guidance](https://developers.home-assistant.io/docs/device_registry_index/) ties entities to stable device identifiers.

## Polling, availability and product scope

F10.2 uses one coordinator per device. It fetches the required system/product snapshots as needed, validates identity/schema and publishes a complete typed Luma view atomically. The regular interval is measured and configured conservatively; a failed poll retains prior values for diagnosis but marks affected entities unavailable once current coherent data cannot be established. A successful HTTP 200 alone is insufficient: required fields, array lengths, numeric bounds and identity consistency must pass. Do not treat `overallHealth=warning` or `DEGRADED` as a communications failure. Device health is a separate diagnostic state. An invalid `timeValid` affects time-derived values, not necessarily every device entity. Recovery after a valid snapshot restores availability. HA's [coordinator pattern](https://developers.home-assistant.io/docs/integration_fetching_data/) owns the refresh and first setup retry behavior.

Minimal **must-have F10.2** Luma read-only entities from the actual status response: mode, active profile (number plus name as an attribute), day/night state, time validity, overall health, and eight final output level sensors with stable physical channel keys. These are status, not controls; no command platform is loaded. **Later**, if useful and validated: eight requested level sensors, global power limit, Wi-Fi connected/state/RSSI and local time (with its validity), plus selected `/api/diagnostics` and firmware/Core version diagnostics from `/api/system`. Do not invent stage, RTC state, hardware health, alarms or a standalone availability endpoint from this status response; those need separately verified API data. Read-only sensors must not turn a displayed percentage into a writable control.

Hydro is a later product adapter (F10.5) using its own bounded status/diagnostic projection and safety meanings. Doser's F10.6 adapter is a controlled replacement after the new integration is proven feature by feature against the current approximately 205 MQTT entities; preserve PubSubClient, MqttManager, HaDiscovery, MQTT Discovery and all current functions until that cutover. No MQTT work or removal belongs to this gate.

## Realtime boundary and tests

F10.3 may add a Realtime subscriber beneath the same coordinator interface. It must wait for a reviewed product server cutover and public wire/auth contract; the Core F9.6 fixture alone is not that deployment. Connect receives `StreamStart` with `RuntimeIdentity` and position; fetch a coherent HTTP snapshot and watermark, buffer only a bounded overlap, discard notifications `<= N`, accept only contiguous `> N`, and full-resync on gap, reconnect, runtime change or lost continuity. WS is a refresh signal and never snapshot authority. Entity classes continue consuming the coordinator view. Product server cohort, metadata, notifications and recovery are separate prerequisites for using F10.3 on a product.

Future tests: pure client tests with fake HTTP responses for bounded JSON, timeouts, malformed fields, status/error mapping and protocol rejection; Config Flow tests for success, duplicate, unreachable, invalid identity/product, unsupported protocol and host update; coordinator tests for valid snapshot, timeout, invalid/coherence failure and recovery; Luma adapter tests for eight-channel mapping and stable keys. Later Realtime tests cover gap, runtime change, bounded overlap and reconnect. HA-specific flow/entity tests can use HA test fixtures; pure client/adapter tests need no HA instance. F10.1A is docs-only and runs no build, flash or HIL.

## Server gap classification and next step

| Class | Requirement | Reason |
| --- | --- | --- |
| **BLOCKER FOR F10.2** | F10.1B publishes canonical stable `device_id` and `device_type` from accepted IDN-101 ownership in `/api/system`, with failure handling and host/physical stability evidence. | Prevents duplicate entries and changing entity IDs; current API exposes neither canonical ID nor canonical product token. |
| **BLOCKER FOR F10.2** | F10.1B publishes and documents `api_protocol_version` plus Luma status schema compatibility. | Client cannot safely select an adapter or reject incompatible firmware otherwise. |
| NICE TO HAVE | Discovery service/TXT, configuration URL metadata, finer hardware/model description, read-only capability flags. | Manual flow and static adapter work without them. |
| FUTURE | `/api/capabilities`, production product WS/cohort/wire/auth, dynamic entities, Hydro/Doser adapters, SEC-101 final auth and Doser MQTT retirement. | Needed at later gates, not for Luma polling. |

**Exact next step:** F10.1B server identity/API prerequisite, scoped to the common system projection/serialization and Luma compatibility contract with tests and product composition evidence. After its checkpoint, implement the shared Python client and F10.2 Luma read-only flow/entities in the accepted F10 sequence. F10.1A does not implement F10.1B or F10.2.
