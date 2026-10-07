# LumaSense — Home Assistant target

## 1. Current state

Luma has native local HTTP and is polling-only. It does not currently implement the target
Home Assistant integration or a production Realtime stream. The integration described here
is FUTURE and follows `docs/HOME_ASSISTANT_INTEGRATION_STANDARD.md` and HA-101.

Luma remains autonomous. Home Assistant, LAN, WebSocket and Internet availability cannot
affect RTC, schedules, profiles, transitions or outputs.

## 2. Target boundary

```text
Home Assistant custom aquaOne integration
→ local HTTP snapshots and HTTP POST actions
→ Luma Application boundary
→ RuntimeState/snapshot as confirmation

Luma Realtime stream
→ change notification and F9.6 resync
→ HTTP snapshot remains the source of truth
```

Home Assistant must not access GPIO, LEDC or mutable internal state. Commands use the same
validated Application and safety paths as local Web. Protocol v1 carries no commands over
WebSocket.

## 3. Candidate v1 projection

The future read-only adapter may expose availability, status, mode, active profile,
DAY/NIGHT, stage, time validity, RTC state, eight requested/final channels, hardware health,
firmware/Core versions and selected RSSI/IP diagnostics.

The initial command set may include profile selection and NORMAL/SERVICE/OFF through existing
or future legal HTTP action endpoints. Restart remains later system workflow work.

The adapter uses stable DeviceIdentity/future DeviceId and `api_protocol_version`; it does not
use an IP address as identity. Config Flow with Zeroconf and manual host fallback is the target
setup flow.

## 4. Consistency and availability

Until Luma Realtime cutover, Home Assistant may poll coherent HTTP snapshots. After cutover,
connect/reconnect/gap recovery uses StreamStart, snapshot watermark and bounded overlap from
F9.6. WebSocket is a notification stream, not state authority.

A successful snapshot proves recent reachability. WebSocket disconnect triggers
reconnect/resync and does not represent a Luma Domain failure.

## 5. Out of scope for this checkpoint

This document does not add firmware, endpoints, Zeroconf, authentication, entity code or
Realtime enablement. Exact entity identifiers and discovery TXT records remain Phase 10 work.
