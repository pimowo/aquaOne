# F10.2 — Luma read-only Home Assistant HIL

**Date:** 2026-10-09
**Verdict:** PASS for F10.2 read-only scope
**Evidence source:** operator-observed Home Assistant and physical-device HIL, reported for this checkpoint
**Repository baseline:** `a9ceb3f56277638d9be00d149337431aa98a8a2b` (`main == origin/main` before checkpoint documentation)
**Firmware/source changes during HIL:** none reported
**Home Assistant Core version:** not captured
**HA installation type:** not captured

## Device and integration

The `aquaone` custom integration was copied to `/config/custom_components/aquaone`;
Home Assistant Core restarted successfully and exposed `aquaOne` in the integration
catalog. Manual setup against the production classic ESP32 Luma completed. The HA
Device Registry showed one Luma device with manufacturer `aquaOne`, software version
`0.2.1`, and hardware variant `LOLIN32_TEST`.

The HIL report did not include the device MAC, device ID, configured address, or
credentials. They are intentionally omitted here.

## Observed entities and semantics

Exactly 13 sensors appeared with Polish translations:

- Aktywny profil
- Obliczone wyjście kanału 1 through 8
- Poprawność czasu
- Pora dnia
- Stan systemu
- Tryb

Values were visible and readable. The active profile was 1; mode was `Normalny` and
day state was `Dzień`. Time validity was `Niepoprawny` and system health was `Błąd`.
Despite those device-level semantic states, the entities remained available. This
confirms that invalid device time and a reported health error do not by themselves
mean that the HTTP coordinator has lost transport availability.

## Lifecycle and duplicate behavior

| Scenario | Operator-observed result |
| --- | --- |
| Add the same device again | UI reported “To urządzenie jest już skonfigurowane”; no duplicate entry, device, or entities appeared. |
| Power off/disconnect the Luma | All 13 entities became unavailable; the HA integration, config entry, and device remained. |
| Restore the same Luma | Existing entities recovered automatically without a duplicate. |
| Reload the integration | PASS; same device and entities remained usable. |
| Restart Home Assistant Core normally | PASS; `aquaOne` loaded automatically and the same device/entities returned. |

## Limits and unrun checks

- Exact passive HTTP request count was **not directly measured**. The configured polling
  interval and the implementation's request paths are software properties, not a
  hardware-observed request count.
- A live synthetic server identity mismatch was **not run**.
- The English live translation was not confirmed in the reported HIL.
- Home Assistant Core version and installation type were **not captured**.
- No firmware flash, firmware modification, or source/test change was part of the HIL.
- The HIL establishes behavior in the operator's running HA environment; it does not
  establish compatibility with an uncaptured Core version matrix.

## Checkpoint status

F10.2 is CLOSED/CURRENT for the Luma read-only polling scope. Phase 10 remains
IN PROGRESS. F10.3 is REQUIRED NEXT after review of the Luma product server's
WebSocket composition, coherent snapshot cohort, and public wire contract. This HIL
does not establish product Realtime availability. Luma commands are not implemented;
Hydro and Doser remain later adapters, Doser MQTT/Discovery remains LEGACY CURRENT
until F10.6, and SEC-101 remains OPEN.
