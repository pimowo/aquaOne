# Roadmap — Plan prac ekosystemu aquaOne

Roadmap opisuje logiczną sekwencję prac. Terminy są elastyczne; kolejność jest stała.

## Faza 1: Dokumentacja i kontrakt (OBECNA)

**Cel:** Utrwalić architekturę, kontrakt API, standard projektów.

- ✅ Audyt Core (NTP, RTC recovery, Storage, Network, Web)
- ✅ Pełna analiza porównawcza (Core vs Doser)
- ✅ Dokumentacja Architecture.md, PROJECT_MATRIX.md, ROADMAP.md
- ✅ Definicja standard projektu (struktura katalogów, separacja warstw)
- ✅ Weryfikacja EuropeWarsawTimeService (utrzymany w Core)

**Dostarczenia:**
- `aquaOne/README.md` — quickstart
- `aquaOne/docs/ARCHITECTURE.md` — design decisions
- `aquaOne/docs/PROJECT_MATRIX.md` — status matrix
- `aquaOneCore/README.md` — Core API documentation

**Ryzyko:** Brak
**Status:** In Progress

---

## Faza 2: Gas — Czysty klient Core (próba integracji)

**Cel:** Pierwszy projekt (po Luma) który w pełni integruje Core.

1. GasSenseApp::begin() — faktycznie inicjalizuje Core
2. Config + Storage (jeśli potrzebne do persistencji konfiguracji)
3. Network + Web (jeśli potrzebne do dostępu do UI)
4. Diagnostics snapshot w Web UI (jeśli potrzebne)

**Wymagania:**
- Dokumentacja Core gotowa (Faza 1)
- Gas kod review (architektura już OK)

**Dostarczenia:**
- aquaOneGas v1.0-alpha (Core integration)
- Test report

**Ryzyko:** Niskie

---

## Faza 3: Hydro — Opcjonalne dodatki

**Cel:** Wzbogacić Hydro o Logger (opcjonalnie).

1. Dodać AquaCore::Logger do HydroSenseApp (jeśli to się przydaje)
2. Weryfikacja bieżącej integracji bez zmian kodu logiki

**Wymagania:**
- Hydro już działa dobrze
- Logger nie zmieni logiki

**Dostarczenia:**
- aquaOneHydro v2.1 (Logger integration) — opcjonalnie

**Ryzyko:** Bardzo niskie (additive)

---

## Faza 4: Doser — Migracja do Core (wieloetapowo)

**Cel:** Doser przechodzi na Core bez zepsucia działania.

### Etap 4a: Network migration
1. Dodać AquaCore do platformio.ini (lib_extra_dirs)
2. Parallel: WiFiManager + NetworkService
3. Switch na NetworkService, usunąć WiFiManager
4. Test na hardware

### Etap 4b: Storage/Config migration
1. Adapter StorageManager → StorageService
2. Backward compatibility: load v1 format
3. Switch, test

### Etap 4c: Time migration
1. RtcService (plain read)
2. NtpService (periodic sync)
3. TimeManager → adapter (keep for now)
4. Test conversions

### Etap 4d: Web migration
1. ✅ W1: WebService + Esp32WebBackend jako jedyny fizyczny serwer HTTP
2. W1.5: trasy restart/OTA Dosera na wspólnym transporcie; polityka i efekty pozostają w Doserze
3. Testy sprzętowe auth, OTA success/failure/abort i opóźnionego restartu
4. W2: pozostałe strony i API produktu po zamknięciu bramy W1.5

### Etap 4e: Cleanup
1. Usunąć reimplementacje (WiFiManager, StorageManager, TimeManager, WebManager)
2. Verify MQTT, diagnostics, scheduler nadal działają
3. Doser v2.0 (Core integration)

**Wymagania:**
- Core stable (Fazy 1-2)
- Gas successful (Faza 2)
- Reverse-commit strategy (test points)

**Dostarczenia:**
- aquaOneDoser v2.0 (Core migration)
- Migration notes

**Ryzyko:** Wysokie (duża zmiana)
**Strategy:** Reverse-commit na każdym etapie; hardware tests

---

## Faza 5: Luma — Bez zmian (utrzymanie)

**Cel:** Luma pozostaje referencją bez dużych zmian.

Opcjonalnie:
- Update dokumentacji (jeśli zmieni się Core API)
- Performance tuning (jeśli potrzebna)
- Nowe funkcje domeny (nie zmiany Core)

**Ryzyko:** Brak (no changes)

---

## Faza 6: Clima — Nowy projekt ze standardem

**Cel:** Clima buduje się od razu ze standardem.

1. Wzorzec: struktura jak Hydro/Gas
2. Zdefiniować domenę (temp control, cycles, alarms)
3. Integracja Core: wyłącznie potrzebne moduły
4. Test

**Wymagania:**
- Projekt architektoniczny
- Standard proyecto gotowy (Faza 1)

**Dostarczenia:**
- aquaOneClima v0.1-alpha

**Ryzyko:** Niskie (świeży projekt)

---

## Faza 7: Fauna — Nowy projekt ze standardem

**Cel:** Fauna buduje się od razu ze standardem.

Podobnie jak Clima.

**Wymagania:**
- Projekt architektoniczny
- Standard proyecto gotowy (Faza 1)

**Dostarczenia:**
- aquaOneFauna v0.1-alpha

**Ryzyko:** Niskie (świeży projekt)

---

## Faza 8: MQTT i Home Assistant (FUTURE)

**Cel:** Dodać integrację MQTT/HA do Core.

Składowe:
- MqttService + MqttBackend
- T0: spike linkowania i lifecycle natywnego ESP-MQTT na ESP32/ESP32-S3
- Esp32MqttBackend (ESP-MQTT za neutralnym interfejsem; decyzja finalna po T0)
- HA discovery helpers
- Entity registration mechanism
- Minimalny read-only adapter Dosera: 5 encji na pompę + `automatic_dosing`

**Wymagania:**
- Doser migracja (Faza 4) — reference implementation
- Core stable (Fazy 1-7)

**Dostarczenia:**
- Raport T0 z QoS 1/PUBACK, reconnect, LWT, RAM i Flash
- AquaCore with MQTT support
- MQTT integration guide

**Ryzyko:** Medium
**Status:** FUTURE; T0 przed implementacją Core MQTT. `text` i `time`: CAN WAIT.

---

## Faza 9: OTA i Watchdog (FUTURE)

**Cel:** Dodać OTA firmware updates i watchdog recovery.

Składowe:
- OtaService + OtaBackend
- Rollback safety
- Watchdog / restart recovery

**Wymagania:**
- MQTT stable (Faza 8, jeśli potrzebne)
- Core stable

**Dostarczenia:**
- AquaCore with OTA support

**Ryzyko:** Medium (critical function)
**Status:** FUTURE (brak konkretnej wersji)

---

## Candidates & Backlog

### RTC Health Monitoring (OPTIONAL)

**Cel:** Opcjonalny mechanizm recovery dla RTC (z Dosera do Core).

Jeśli decyzja będzie dodać:
1. Zdefiniować RtcHealthMonitor interface
2. Implementacja: failure/success thresholds (recovery logic)
3. Cache + interpolacja czasu
4. Integracja do projektów które to potrzebują

Jeśli decyzja będzie pominąć:
- Doser używa swój TimeManager
- Luma i inne nie potrzebują

**Status:** BACKLOG (do podjęcia decyzji)

---

## Timeline — Orientacyjne (ELASTYCZNE, BEZ TERMINÓW)

```
Faza 1 (Dokumentacja)   ███████ ← TERAZ
Faza 2 (Gas)            ░░░░ (parallel z Faza 1 end)
Faza 3 (Hydro)          ░░░░░░ (after Faza 2, optional)
Faza 4 (Doser)          ░░░░░░░░░░░ (long, many stages)
Faza 5 (Luma)           ─────── (maintenance only)
Faza 6 (Clima)          ░░░░░░░░░░░ (when ready)
Faza 7 (Fauna)          ░░░░░░░░░░░ (when ready)
Faza 8 (MQTT)           ░░░░░░░░░░░░░░░ (future)
Faza 9 (OTA)            ░░░░░░░░░░░░░░░░░ (future)
```

**Oznaczenia:**
- `███` — Active work
- `░░░` — Planned, waiting
- `───` — Maintenance only
- ← TERAZ — Current phase

---

## Kryteria sukcesu każdej fazy

| Faza | Kryterium |
|------|-----------|
| 1 | Dokumentacja complete, brak open questions |
| 2 | Gas integrated, tests passing, zero regression |
| 3 | Hydro enhanced, optional Logger, no regression |
| 4 | Doser migrated, all functionality preserved, hw tests OK |
| 5 | Luma stable, no regression |
| 6 | Clima running, tests passing |
| 7 | Fauna running, tests passing |
| 8 | MQTT stable, entity registration working |
| 9 | OTA updates working, recovery tested |

---

## Metryki do śledzenia

- **Build time** — Nie powinna rosnąć (Core size)
- **Flash usage** — Per-device breakdown
- **Regression** — Changelog dla każdego projektu
- **Migration risk** — Test points dla Dosera
