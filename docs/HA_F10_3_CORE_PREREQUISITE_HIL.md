# F10.3 Core prerequisite — implementation and physical HIL

**Date:** 2026-10-09
**Baseline:** `34efc4381893fef4dd5c1dd2cb58f610758aafd7`
**Verdict:** PASS; F10.3 Core prerequisite CLOSED/CURRENT
**Next:** F10.3B — Luma server Realtime composition

## Scope and candidate integrity

F10.3-CORE1 implemented the reusable Core StreamStart TEXT JSON encoder and
per-product `api_protocol_version` selection. It did not implement Luma product
Realtime, change product or HA source, or advance any product to API 1.1.

The ten candidate paths were:

- `aquaOneCore/include/AquaCore/Web/ApiProtocolVersion.h`
- `aquaOneCore/include/AquaCore/Web/CoreWebProjectionSources.h`
- `aquaOneCore/include/AquaCore/Web/CoreWebProjections.h`
- `aquaOneCore/include/AquaCore/Web/EspIdfWebTransport.h`
- `aquaOneCore/include/AquaCore/Web/RealtimeStreamStartWire.h`
- `aquaOneCore/src/Web/CoreWebProjectionSources.cpp`
- `aquaOneCore/src/Web/EspIdfWebTransport.cpp`
- `aquaOneCore/test/test_web_native_integration/test_main.cpp`
- `aquaOneCore/test/test_web_realtime_resync_foundation/test_main.cpp`
- `aquaOneCore/test/test_web_realtime_resync_hil/hil_host.py`

SHA-256 was recorded before HIL outside the repository and recomputed after HIL
and Luma restoration. All ten files matched. The generated
`aquaOneCore/test/test_web_realtime_resync_hil/hil_results.json` remains local
and untracked; it was not part of the candidate or checkpoint.

| Candidate path | SHA-256 |
|---|---|
| `aquaOneCore/include/AquaCore/Web/ApiProtocolVersion.h` | `D75B6C7D8D4266D104DBF13CCF60231790CB0606D458108CABDCE95CECFCF9FE` |
| `aquaOneCore/include/AquaCore/Web/CoreWebProjectionSources.h` | `45279A093740103616C12D0E50085E51EF53C3D88AAEC2366F6B9A707ACD5548` |
| `aquaOneCore/include/AquaCore/Web/CoreWebProjections.h` | `1BEB7C5734AA1B771B8F619AA57588BEE81BFB6AE918E53553ECE2F2774B7114` |
| `aquaOneCore/include/AquaCore/Web/EspIdfWebTransport.h` | `FE3FA24B743D3E0F4BDA04393375C6FF9D4EFBAE04414B6C541DF43AC4AB5A71` |
| `aquaOneCore/include/AquaCore/Web/RealtimeStreamStartWire.h` | `A112B2C75791F954578B661FAEAFAC5F7B1AF5F5360F149AA984EAD9401FD4AA` |
| `aquaOneCore/src/Web/CoreWebProjectionSources.cpp` | `F6411772330E12CCE5F72096372417659A691A4AB700A5DEB4E231873275DAF7` |
| `aquaOneCore/src/Web/EspIdfWebTransport.cpp` | `E0D87A0C41FBB4BF2DBE16FC30EB292E298AD6A57059A3224706F7EF18D5D9FE` |
| `aquaOneCore/test/test_web_native_integration/test_main.cpp` | `CF0E1AA63E8161698CD704D891AC47C2DCA9A82FB1FBE5CF0DA7412142A5BB80` |
| `aquaOneCore/test/test_web_realtime_resync_foundation/test_main.cpp` | `9B514D58FCF96786F06503B70530606033BD24B9DF2D4EE82985F147AAEFFB20` |
| `aquaOneCore/test/test_web_realtime_resync_hil/hil_host.py` | `AA7A027E8D9A9D30BF6D2A303894715A5013794B0F961AF99C7C23C34FF1F126` |

## Core contract

The public Core StreamStart is one UTF-8 JSON object in a WebSocket TEXT frame.
The encoder uses `RuntimeIdentity::format()` for exactly 16 uppercase hex
characters and writes a canonical decimal-string sequence. Its exact fixture
frames are:

```json
{"type":"stream_start","runtime_id":"0123456789ABCDEF","position":{"kind":"before_first"}}
{"type":"stream_start","runtime_id":"0123456789ABCDEF","position":{"kind":"at","sequence":"1"}}
{"type":"stream_start","runtime_id":"0123456789ABCDEF","position":{"kind":"at","sequence":"18446744073709551615"}}
```

Their wire lengths are 90, 95 and 114 bytes. The caller-owned payload capacity
is 128 bytes; the helper reserves one additional byte for its local NUL, which
is excluded from the WebSocket frame length. It uses bounded stack/caller
storage and no dynamic allocation. Failure sets length to zero and leaves the
caller buffer unchanged.

The transport queues the copied immutable StreamStart state while the client is
CONNECTING. Only successful TEXT marker delivery can move the client to LIVE.
Marker encode, queue or send failure leaves it non-LIVE. Notifications retain
the caller-selected `RealtimeFrameType::Text` or `Binary`. Recovery generations,
client/fd tokens, sticky recovery, inactive-fd reclaim and close handling retain
their existing behavior.

Current implementation capacities are four notification work slots with 256 B
payloads and seven client slots. They are implementation capacities, not public
protocol limits. On the classic ESP32 compiler, `StreamStartWork` measured 96 →
200 B, its seven slots 672 → 1400 B, and `EspIdfWebTransport` 6992 → 7720 B
(+728 B total).

## Software and production verification

| Check | Result |
|---|---|
| Focused Core native tests | 25/25 PASS |
| Full configured Core native tests | 386/386 PASS |
| Hydro native tests | 9/9 PASS |
| Doser native tests | 37/37 PASS |
| Luma Web test compile/link | PASS; not executed on-device |
| Core ESP32 HIL fixture compile/link | PASS; upload and physical run recorded below |
| Luma production `esp32dev` | PASS; RAM 63,764 B, Flash 1,062,000 B |
| Hydro production `esp32s3` | PASS; RAM 55,936 B, Flash 992,016 B |
| Doser production `esp32-s3-super-mini` | PASS; RAM 60,748 B, Flash 1,094,636 B |
| HIL host parser | Python syntax and valid/invalid TEXT plus binary notification fixtures PASS |

The builds used pioarduino 53.03.13 and Arduino-ESP32 3.1.3 (ESP-IDF 5.3.2).
The physical upload used esptool 4.8.6. No test firmware or production firmware
was erased from flash as a whole; production Luma was restored by normal upload.

## Physical Core HIL

The fixture ran on COM6, ESP32-D0WD-V3 revision 3.1. Serial reported
`HIL_READY`; the sanitized initial internal heap sample was 225,284 B free,
225,240 B minimum and 110,580 B largest block. No actual runtime identity or
device MAC is recorded here.

| Scenario | Result |
|---|---|
| Fresh connection marker | TEXT JSON `BeforeFirst`, exact 90-byte wire frame |
| One transition | Binary fixture notification sequence 1; later client received exact 95-byte TEXT `At("1")` marker |
| First-frame ordering | Every tested new client received StreamStart before notification |
| Same listener | HTTP HIL routes and WS worked on the same HTTPD port |
| Two clients and fanout | PASS; independent close/lifecycle and survivor continuity PASS |
| Reconnect/resync | Fresh TEXT marker and coherent HTTP snapshot PASS |
| Runtime restart | Runtime identity changed; stream reset to BeforeFirst and resync PASS |
| Snapshot failure | Notification withheld; recovery and republish PASS |
| Notification failure | Sticky recovery and repair PASS |
| Real work-pool saturation | `Busy` observed at the fifth attempt; recovery requested and repaired |
| Slow client | PASS; 271 normal-client frames, 8 HTTP samples; recovery engaged for slow client |
| HTTP under WS load | Requests continued through concurrent WS activity; 168 requests in stability scenario |
| Mixed stability | 60.09 s; 84 notifications; 168 HTTP requests; 4 reconnects |

HIL heap samples ended at 223,096 B free internal heap, with 204,932 B minimum
and 110,580 B largest block. The fixture completed without an unexpected
runtime change during the run. Serial was not recorded continuously for the
entire HIL, so absence of panic, watchdog or brownout is limited to observed
serial output and the fixture's continuity/stability checks.

## Production Luma and Home Assistant restoration

After HIL, production Luma was reflashed on the same COM6 board without erasing
NVS. Wi-Fi returned. `GET /api/system` returned 200 with `device_type: luma` and
`api_protocol_version` 1.0. `GET /api/lumasense/status` returned 200 without
Realtime metadata, and `/ws/realtime` returned 404. This confirms CORE1 alone
did not cut Luma over to product Realtime.

The operator confirmed Home Assistant recovery: the existing Luma and
integration entry remained, all 13 of 13 entities became available again, and
no duplicate device, entry or entity set appeared.

## Current status and next boundary

- F10.3A: CLOSED.
- F10.3 Core prerequisite: CLOSED/CURRENT.
- Core public StreamStart: TEXT JSON CURRENT; generic notifications remain selectable TEXT/BINARY.
- `DEFAULT_API_PROTOCOL_VERSION`: 1.0; version selection is per product/composition, without mutable global state.
- Luma, Hydro and Doser production APIs remain 1.0. Luma 1.1 is not CURRENT.
- Luma product WebSocket, status watermark and `luma_status_changed` remain NOT CURRENT.
- HA WebSocket/Realtime client remains NOT CURRENT.
- F10.3B is REQUIRED NEXT: Luma server Realtime composition.

F10.3B owns fresh runtime identity composition, the sequencer, a one-resource
`RealtimeSnapshot<LumaStatusProjection>` cohort, `/ws/realtime`, Luma-only API
1.1, status Realtime metadata, `luma_status_changed`, and the product recovery/
lifecycle supervisor. This checkpoint does not implement those changes.
