# HOME_ASSISTANT_INTEGRATION_STANDARD.md

**Status:** TARGET / ACTIVE DESIGN; F10.1B server prerequisite CURRENT
**Scope:** aquaOne ecosystem
**Decision:** HA-101 ACCEPTED
**Version:** 1.0
**Last reviewed:** 2026-10-09

## 1. Purpose and autonomy

The target Home Assistant integration is one custom integration named `aquaOne` for Luma,
Hydro, Doser, Gas, Clima and Fauna. It is a local client of device Application boundaries.
It is never the authority for Domain state or safety.

Every device MUST keep its primary function without Home Assistant, LAN, WebSocket,
Internet, another aquaOne device or any integration service. Failure or absence of Home
Assistant MUST NOT stop Domain processing.

MQTT, an MQTT broker and MQTT Discovery are not part of the aquaOne TARGET. The Doser MQTT
implementation remains LEGACY CURRENT until its later product migration. No shared AquaCore
MQTT module is planned.

## 2. Transport model

The custom integration reuses the same product and Core boundaries as local Web and a future
Panel. It does not add a special device transport.

| Operation | TARGET transport | Rule |
|---|---|---|
| Read | local HTTP GET snapshots | coherent snapshot is the source of truth |
| Change notification | WebSocket/Realtime | bounded notification stream, never sole authority |
| Command/config/system action | HTTP POST action endpoint | normal Application command and policy path |

Domain commands MUST NOT be sent over WebSocket in protocol v1. A successful network write
does not prove semantic command success. The HTTP result reports the semantic outcome; the
subsequent device snapshot confirms actual state. An accepted timeout means outcome unknown.
The integration MUST NOT automatically retry a non-idempotent action. CMD-102 remains binding.

## 3. Snapshot and realtime consistency

After connect, reconnect, a sequence gap, runtime identity mismatch or continuity loss, the
integration MUST reuse the F9.6 resync model:

1. receive `StreamStart` and current stream position;
2. fetch a coherent HTTP snapshot with its watermark;
3. retain only a bounded overlap of changes;
4. discard changes at or below the snapshot watermark;
5. apply only contiguous changes above that watermark.

An implementation MUST fall back to another snapshot when continuity cannot be proven. It
MUST NOT invent a second Home Assistant specific consistency model.

## 4. Integration structure and product adapters

The Home Assistant repository owns product aware adapters. Product branching there does not
violate the Core rule that AquaCore has no Domain knowledge. Each device may expose its own
bounded, versioned product status projection; a monolithic cross-product JSON document is not
required.

The v1 integration maps a stable `device_type` to a known product adapter and checks a
versioned product API. `GET /api/capabilities` remains FUTURE until a concrete need justifies
the added generic manifest and dynamic entity machinery.

## 5. Setup, discovery and identity

Config Flow is the primary setup path. Manual hostname or IP entry MAY be offered as a
fallback. YAML is not the primary setup contract.

Discovery is local Zeroconf/DNS-SD. `_aquaone._tcp.local.` is the TARGET candidate service
type. It follows the DNS-SD `<Service>.<Domain>` form in RFC 6763 and the `aquaone` service
label fits the RFC 6335 syntax and 15-character limit. Exact registration, instance naming,
port and TXT schema remain a Phase 10 spike and are not frozen by this document. Zeroconf is
discovery, not authentication.

The Home Assistant device registry MUST use the accepted stable `DeviceIdentity` pair.
F10.1B now exposes `device_type` and a public `device_id`: the 12-character uppercase
representation of the factory/default base MAC48 for the current ESP32-family implementation.
It is a technical identifier, not an authentication credential. IP address and hostname
locate the current endpoint; they are not device identity. Other hardware platforms need
their own reviewed source before claiming this contract.

## 6. Initial connection

The target sequence is:

1. discover or manually configure the device;
2. fetch the Core identity/system snapshot;
3. identify `device_type` and API compatibility;
4. establish the Realtime connection;
5. perform the F9.6 resync handshake;
6. fetch the product snapshot or snapshots;
7. create or update product entities;
8. enter live mode.

A CURRENT polling-only product MAY use periodic HTTP snapshots until its Realtime cutover.
Luma and Hydro are CURRENT native HTTP and polling-only; Doser remains on its
legacy MQTT and MQTT Discovery implementation.

## 7. Version and compatibility contract

The minimum neutral compatibility tuple is stable `device_type`, firmware version, Core
version and `api_protocol_version`. F10.1B CURRENT `GET /api/system` publishes canonical
`device_type`, `device_id` and `api_protocol_version` as an object with `major: 1` and
`minor: 0`; legacy system fields remain present. Luma's current read-only status schema
belongs to this API major. The target contract MUST NOT use `mqtt_protocol_version`.

The client MUST reject unsupported major protocol or product API versions clearly and MUST
not guess a schema. Backward compatible additions MAY use minor capability checks owned by
the product adapter.

## 8. Availability and reconnect

A successful coherent snapshot proves that the device was reachable and current at that
point. Realtime session state supplements this fact. A WebSocket disconnect starts
reconnect/resync; it does not imply Domain failure. No arbitrary availability timeout is
standardized before measurement in the Phase 10 spike.

Device work never depends on the Home Assistant availability state. MQTT LWT is not part of
the TARGET availability model.

## 9. Alarms, diagnostics and entities

Alarms originate locally. HTTP exposes current active alarm state, while Realtime reports
alarm and state changes. A later ACK feature MUST use the normal authorized HTTP command path
and MUST NOT create an alarm-specific transport.

Home Assistant should expose useful user state plus selected fields with
`entity_category=diagnostic`. Full technical diagnostics remain available through Web/support
snapshots. An adapter should normally expose tens of purposeful entities, not every stored
configuration field and not the legacy Doser set of about 205 MQTT entities.

The first Luma adapter candidate includes availability, status, mode, active profile,
DAY/NIGHT, stage, time validity, RTC state, eight requested/final channels, hardware health,
firmware/Core versions and selected network diagnostics. Initial commands are profile and
NORMAL/SERVICE/OFF through legal HTTP actions.

The later Hydro adapter candidate includes tank level, reserve, float, pump state, pump
allowed, lockout, top-up state, available temperature, alarms, buzzer mute state, service
state and selected diagnostics. Existing F9.7E1 decisions remain unchanged.

Doser keeps PubSubClient, MqttManager and HaDiscovery as LEGACY CURRENT until F10.6 replaces
them with the HTTP/API product adapter. That migration must preserve functionality during the
cutover and does not justify new MQTT work in Core.

## 10. Security and locality

SEC-101 remains OPEN. The integration must eventually use the accepted authentication and
session model. LAN location does not imply permanent trust. Discovery metadata MUST NOT grant
command authority.

The target is local-first and requires no cloud, vendor account, Internet, Nabu Casa or MQTT
broker. Remote access is owned by Home Assistant and the user, not by an aquaOne device.

## 11. Phase 10 scope

Phase 10 is **Home Assistant Integration**:

- **F10.1 / HA.1:** integration architecture and reusable HTTP client library;
- **F10.2:** Luma read-only polling spike and product adapter;
- **F10.3:** Realtime client, reconnect and F9.6 resync;
- **F10.4:** Luma HTTP commands and semantic confirmation;
- **F10.5:** Hydro product adapter after its native Web migration;
- **F10.6:** Doser replacement and retirement of product-local MQTT/Discovery.

Exact entity schemas, Zeroconf TXT keys, authentication details and availability timing need
their dedicated Phase 10 decisions or measurements. This architecture checkpoint does not
implement them.

## F10.1A gate (2026-10-09)

[F10.1A](HA_F10_1A_ARCHITECTURE.md) starts Phase 10 as a docs-only architecture gate
and selects **B — SERVER PREREQUISITE REQUIRED**. At that gate `/api/system` did not
publish canonical stable `device_id` or `api_protocol_version`, and its legacy
`deviceType` values are not the IDN-101 product tokens. F10.1B must establish and
publish those fields before F10.2 creates stable HA entries/entities. No integration,
firmware API or product Realtime changed at this gate; Doser MQTT remains LEGACY CURRENT.

## F10.1B server checkpoint (2026-10-09)

F10.1B is CLOSED/CURRENT after native tests and physical identity, stability and reboot
checks on classic ESP32 Luma and ESP32-S3 Doser. The common system API now supplies the
stable identity and version prerequisite for F10.2. Phase 10 remains IN PROGRESS;
F10.2 Luma read-only integration is now CLOSED/CURRENT after real Home Assistant HIL.
See [F10.2 evidence](HA_F10_2_LUMA_READONLY_HIL.md). Product WebSocket,
`/api/capabilities` and Zeroconf service are not CURRENT. SEC-101 remains OPEN;
Doser MQTT/Discovery remains LEGACY CURRENT until F10.6.


## F10.2 Luma read-only checkpoint (2026-10-09)

The `aquaone` custom integration installs and loads in the operator's Home Assistant
environment, completes manual setup against the production classic ESP32 Luma, and
creates one device with 13 read-only sensors. Operator-reported HIL covered duplicate
setup rejection, unavailable state while the same device was powered off, automatic
recovery, integration reload and normal Core restart. `timeValid=false` and health
error were visible while entities remained available, confirming that device semantics
are separate from transport availability. The exact HA Core version and passive HTTP
request count were not captured; live synthetic identity mismatch was not run.

The checkpoint closes F10.2 for its read-only scope. Phase 10 remains IN PROGRESS;
F10.3 is REQUIRED NEXT only after review of the Luma server's product WebSocket,
cohort and public wire contract. No HA WebSocket subscriber or Luma commands are
CURRENT. F10.4 commands, F10.5 Hydro, F10.6 Doser MQTT replacement, `/api/capabilities`
and Zeroconf remain future. SEC-101 remains OPEN.
