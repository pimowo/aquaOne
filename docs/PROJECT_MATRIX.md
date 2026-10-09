# Project Matrix — Status i Integracja Core

**Snapshot date:** 2026-10-09
**Snapshot base commit:** 2259695 plus F9.8B closure

Ten dokument opisuje wyłącznie stan zaimplementowany w lokalnym kodzie dla wskazanego
commita. Nie definiuje architektury docelowej ani kolejności przyszłych prac.

F9.8B: Phase 9 is CLOSED. Core Realtime WS/resync is CURRENT and passed classic
ESP32 HIL; Luma, Hydro and Doser production HTTP is native, while product
WS/cohorts/notifications are absent. Phase 10 is IN PROGRESS with F10.1A
docs-only architecture complete; HA implementation is not started. F10.1B
server identity/API prerequisite is required before F10.2.
See `docs/WEB_F9_8B_PHASE9_CLOSURE.md` and the detailed
`docs/WEB_F9_8A_PHASE9_CLOSURE_AUDIT.md`; F10.1A decision:
`docs/HA_F10_1A_ARCHITECTURE.md`.

## HA-101 target update

Home Assistant TARGET is one local custom `aquaOne` integration using HTTP snapshots,
HTTP POST actions and WebSocket/Realtime. AquaCore MQTT is not a planned module. Luma and
Hydro are future product adapters after their native API readiness. Doser MQTT/Discovery is
LEGACY CURRENT and remains functional until F10.6 replaces it. Gas, Clima and Fauna are future
HA/API adapters. These TARGET statements do not change any CURRENT implementation below.

## F9.7D2 update

F9.7D is CURRENT/CLOSED after the native production Web HIL and checkpoint.
Luma uses one `EspIdfWebTransport` plus `NativeWebService`; it remains
polling-only with a transitional Application bridge. Real HTTP/Wi-Fi evidence is
sanitized in `docs/WEB_F9_7D2_LUMA_HIL.md`. WEB-101 remains DECISION REQUIRED
and SEC-101 remains OPEN. The 250 ms wait is provisional: HIL recorded no 202,
with p95 504.565 ms and maximum 561.396 ms host-observed HTTP latency.

## F9.7E2/F9.7E3 update

Hydro now uses one `EspIdfWebTransport` plus `NativeWebService`. Pages and read
APIs consume fixed published projections; control and settings use a
fixed-capacity Application bridge with at most one request processed per loop.
Hydro stays polling-only. The global normal POST storage is 1,536 bytes for
Hydro settings, while all three Luma POST routes retain explicit 512-byte
limits. F9.7E3 bare-board Web/Application HIL passed with expected hardware
limitations; full physical Hydro hardware HIL remains future work.

## Legenda

- **CORE DIRECT** — projekt bezpośrednio komponuje publiczne API AquaCore;
- **CORE VIA ADAPTER** — lokalna klasa deleguje mechanizm techniczny do AquaCore;
- **LOCAL** — implementacja pozostaje w projekcie domenowym;
- **NOT USED** — moduł nie jest używany przez projekt;
- **PLANNED** — istnieje plan, ale brak implementacji w snapshotcie.

Emoji są tylko pomocą wizualną; tekstowy status jest rozstrzygający.

## aquaOneLuma — 🟢 Lighting Controller (Stabilny)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Platforma** | ESP32 (AQMA, LOLIN32 testowa) | |
| **Status** | ✅ F9.7D CLOSED | Native production Web HIL PASS; sanitized evidence: `docs/WEB_F9_7D2_LUMA_HIL.md` |
| **Architektura** | app/core/hardware/web/time/storage | Transitional product composition, bez pełnego ApplicationRuntime |
| | | |
| **Używane moduły Core** | | |
| System | ✅ READY | systemService (boot, uptime) |
| Config | ✅ READY | StorageService (dual-slot, CRC32) |
| Logging | ✅ READY | Logger + SerialLogSink |
| Diagnostics | ✅ READY | DiagnosticsService agregator |
| Network | ✅ READY | NetworkService (WiFi state machine) |
| Web | F9.7D CURRENT | Jeden `EspIdfWebTransport` + `NativeWebService`; real HTTP/Wi-Fi HIL PASS, Core/Luma snapshots i bounded Application bridge |
| Time | ✅ READY | RtcService, NtpService, EuropeWarsawTimeService |
| | | |
| **Elementy lokalne** | | |
| Logika | LightEngine, TransitionEngine, DayEngine, ModeManager | |
| Konfiguracja | DeviceConfig, ChannelConfig, profiles | |
| Hardware | PWM LED drivers, AqmaHardware, Lolin32Hardware | |
| Web UI | LumaPages + native Luma routes | Polling 1500 ms; bez WebSocket/Auth/OTA |
| | | |
| **Migracja do Core** | F9.7D | Native Web cutover CURRENT/CLOSED po HIL i checkpointcie; polling-only, transitional Application path |
| **Ryzyko** | 🟡 Średnie | HTTPD wait 250 ms pozostaje prowizoryczny; HIL p95 504.565 ms, max 561.396 ms, bez 202 |

---

## aquaOneDoser — 🟡 Nutrient Dispenser (Funkcjonalny)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Platforma** | ESP32-S3 Super Mini (4MB) | |
| **Status** | 🟡 F9.7G4/G5 CLOSED, integracja hybrydowa | Jeden natywny produkcyjny Web; real OTA i bare-board HIL PASS; MQTT pozostaje legacy |
| **Architektura** | Composition root + lokalne managery/adapters | Migracja Core jest częściowa |
| | | |
| **Używane moduły Core** | | |
| System | CORE DIRECT | `SystemService`, `DeviceIdentity` |
| Config | CORE VIA ADAPTER | `StorageManager` używa dwóch `StorageService`; obsługuje też migrację legacy Preferences |
| Logging | CORE DIRECT | `Logger` + `SerialLogSink`; lokalne logi `Serial` nadal istnieją |
| Diagnostics | LOCAL | Lokalny `DiagnosticsManager` |
| Network | CORE VIA ADAPTER | Lokalny `WiFiManager` deleguje do `NetworkService` i `Esp32NetworkBackend` |
| Web | CORE DIRECT | Jeden `EspIdfWebTransport` i `NativeWebService`; lokalna Application obsługuje restart i streaming `POST /update` |
| Time | CORE VIA ADAPTER | Lokalny `TimeManager` komponuje usługi RTC/NTP/resilient time Core |
| | | |
| **Elementy lokalne** | | |
| Logika | PumpManager, SchedulerManager, MqttManager, HaDiscovery | |
| Konfiguracja | `PumpConfig`, dokładnie 8 pomp | |
| Hardware | Relay drivers, PWM pump control | |
| MQTT/HA | LEGACY CURRENT / LOCAL | Istniejące PubSubClient, MqttManager, HaDiscovery i około 205 encji pozostają do migracji Dosera; nie są TARGET Core ani docelowym modelem encji HA |
| **Web W1/W1.5** | DONE, historyczny | Legacy `WebManager`/`DoserWebRuntime` pozostaje w drzewie dla regresji/cleanup, bez produkcyjnego ownership |
| **Web boundary** | F9.7G4/G5 CLOSED | Produktowy WS wymaga osobnego cutover przed użyciem przez klienta; wspólne OTA dopiero Phase 11 |
| **Ryzyko dalszej migracji** | Średnie/wysokie | Lokalna domena działa i nie może zostać naruszona |

---

## aquaOneHydro — 🟢 Water Topup & Level Control (Funkcjonalny)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Platforma** | ESP32-S3 DevKit-C1 | |
| **Status** | F9.7E2 CLOSED | Native HTTP cutover checkpointed; F9.7E3 bare-board Web/Application HIL passed with expected hardware limitations |
| **Architektura** | app/hardware/hydrosense/web | Czysta separacja |
| | | |
| **Używane moduły Core** | | |
| System | ✅ READY | systemService (boot info) |
| Config | ✅ READY | HydroSenseConfigStorage adapter |
| Logging | ❌ NOT USED | Brak logowania |
| Diagnostics | CORE VIA ADAPTER | Native `/api/diagnostics` uses a typed projection of System, Network and Storage facts; the product diagnostics page remains local |
| Network | ✅ READY | NetworkService (WiFi) |
| Web | F9.7E2 CURRENT | One `EspIdfWebTransport` + `NativeWebService`; projection-only reads and bounded Application bridge; polling-only |
| Time | 〰️ NOT NEEDED | Brak wymagań czasowych |
| | | |
| **Elementy lokalne** | | |
| Logika | TopupController, WaterTank, AlarmManager | Domena: zawór+czujniki |
| Hardware | Pump, FloatSensor, UltrasonicSensor, Button, Buzzer | |
| Web UI | Dashboard, SettingsPage, ControlPage, DiagnosticsPage | Shared aquaOne Theme v1; polling-only, without WebSocket/Auth/OTA |
| | | |
| **Brakujące integracje** | | |
| Logging | Opcjonalnie można dodać AquaCore::Logger | Not priority |
| Home Assistant | FUTURE HA/API adapter | Po native Web/Realtime; brak planu MQTT |
| | | |
| **Migracja do Core** | F9.7E2 CLOSED | Native Web cutover is CURRENT after bare-board Web/Application HIL and checkpoint |
| **Ryzyko** | 🟡 Średnie | Product-local 1000 ms action wait remains transitional; full physical Hydro hardware HIL is pending |

---

## aquaOneGas — 🟡 CO2 Bottle Monitor (Budowa)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Platforma** | ESP32-S3 DevKit-C1 | |
| **Status** | 🟡 Budowa | Architektura wzorzec, impl. TODO |
| **Architektura** | app/config/domain/drivers/services/interfaces | Best-in-class separation |
| | | |
| **Używane moduły Core** | | |
| System | ❌ NOT USED | TODO |
| Config | ❌ NOT USED | TODO |
| Logging | CORE DIRECT | `Logger` i `SerialLogSink` są tworzone i używane w `main.cpp` |
| Diagnostics | ❌ NOT USED | TODO |
| Network | ❌ NOT USED | TODO |
| Web | ❌ NOT USED | TODO (GasSenseWeb interface exists) |
| Time | 〰️ NOT NEEDED | Brak logiki czasowej |
| | | |
| **Elementy lokalne** | | |
| Logika | GasCalculator, BottleService, AlarmService, SensorHealthService | Domain: CO2 bottle |
| Hardware | HX711 (weight), ADS1115 (pressure), DS18B20 (temp) | |
| Drivers | WeightDriver, PressureDriver, TemperatureDriver | |
| Interfaces | GasSenseMqtt, GasSenseWeb | |
| | | |
| **Plan integracji** | | |
| Etap 1 | Config + Storage (gdy potrzebne) | Persistence |
| Etap 2 | Network + Web (gdy potrzebne) | UI + diagnostics |
| Etap 3 | HA/API adapter (przyszłość) | Custom Home Assistant integration |
| | | |
| **Migracja do Core** | 🟡 PLANNED | Etapowo, medium risk |

---

## aquaOneClima — 🔴 Climate Control (Planowany)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Platforma** | ESP32-S3 (zaplanowana) | |
| **Status** | 🔴 Szkielet | Tylko main.cpp stub |
| **Architektura** | [Do zdefiniowania] | Będzie wzorowana na istniejących |
| | | |
| **Potencjalne moduły Core** | | |
| System | — | Prawdopodobnie TAK |
| Config | — | Zależy od rodzaju konfiguracji |
| Logging | — | Opcjonalnie |
| Diagnostics | — | Opcjonalnie |
| Network | — | Opcjonalnie |
| Web | — | Opcjonalnie |
| Time | — | Zależy od algorytmu |
| | | |
| **Potencjalna domena** | | |
| Logika | Temp control, cycle scheduling, alarm | [TODO] |
| Hardware | Temp sensors, heater relay, pump relay, fans | [TODO] |
| | | |
| **Migracja do Core** | 🟢 GREEN | Świeży projekt, będzie wdrażany ze standardem |

---

## aquaOneFauna — 🔴 Feeder & Animals (Planowany)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Platforma** | ESP32-S3 (zaplanowana) | |
| **Status** | 🔴 Szkielet | Tylko main.cpp stub |
| **Architektura** | [Do zdefiniowania] | Wzorzec: istniejące projekty |
| | | |
| **Potencjalne moduły Core** | | |
| System | — | Prawdopodobnie TAK |
| Config | — | Zależy od rodzaju konfiguracji |
| Logging | — | Opcjonalnie |
| Diagnostics | — | Opcjonalnie |
| Network | — | Opcjonalnie |
| Web | — | Opcjonalnie |
| Time | — | Zależy od algorytmu scheduling |
| | | |
| **Potencjalna domena** | | |
| Logika | Feeding schedule, portion control, health tracking | [TODO] |
| Hardware | Motor/servo feeder, portion sensor, LED indicator | [TODO] |
| | | |
| **Migracja do Core** | 🟢 GREEN | Nowy projekt, będzie wdrażany ze standardem |

---

## aquaOneCore — 🟢 Shared Technical Foundation (Stabilna)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Typ** | PlatformIO library (library.json) | |
| **Wersja** | 0.6.2 | |
| **Status** | 🟢 CURRENT mixed foundation | Natywne HTTPD i Core Realtime są zaimplementowane; Luma/Hydro/Doser używają Core, ale pełna platforma vNext pozostaje przyszła |
| **Struktura** | `include/AquaCore/<Module>/` + `src/<Module>/` | Implementacje znajdują się bezpośrednio pod `aquaOneCore/src/` |
| | | |
| **Moduły główne** | | |
| System | ✅ READY | SystemService, DeviceIdentity, RestartReason |
| Config | ✅ READY | StorageService (CRC32, versioning, dual-slot) |
| Logging | ✅ READY | Logger + SerialLogSink + compile-time control |
| Diagnostics | ✅ READY | DiagnosticsService (snapshot agregator) |
| Network | ✅ READY | NetworkService + Esp32NetworkBackend |
| Web | ✅ CURRENT | `EspIdfWebTransport` + `NativeWebService`, typed snapshots, bounded actions i Core WS/resync; legacy `WebService`/`Esp32WebBackend` nadal w bibliotece dla regresji |
| Time | ✅ READY | RtcService, NtpService, EuropeWarsawTimeService |
| | | |
| **Nie zaimplementowane** | | |
| MQTT | ❌ | Not planned for TARGET Core |
| OTA | ❌ | Planned (no version assigned) |
| Home Assistant integration | ❌ | TARGET custom HTTP/WS integration (Phase 10) |
| RTC/NTP recovery | ✅ IMPLEMENTED | `ResilientTimeService` z progami failure/recovery i cache czasu |
| | | |
| **Design** | | |
| Backend pattern | ✅ Stosowany | Gdzie potrzebna separacja od platformy |
| Optional modules | ✅ Gwarantowany | Projekty wybierają co potrzebują |
| No domain knowledge | ✅ Gwarantowany | Core nie zna LED, pomp, czujników |
| No device dependencies | ✅ Gwarantowany | Core buduje się niezależnie |
| | | |
| **Zużycie** | | |
| Zweryfikowany | Luma (full) | 7/7 modułów |
| Integracja | Hydro (partial) | 4/7 modułów (bez Time) |
| Budowa | Gas | Core Logging używany; pozostałe moduły zależnie od potrzeb |
| Integracja hybrydowa | Doser | Direct: System/Logging/Web; adapters: Config/Network/Time |
| Planowanie | Clima, Fauna | Projekty startowe bez logiki domenowej |
| | | |
| **Rozwój** | — | Core rozwija się niezależnie; projekty integrują wybrane moduły Core |

---

## Podsumowanie — Integracja vs Status

```
aquaOneLuma    ████████████ ✅ READY    (7/7 Core modules)
aquaOneHydro   ████░░░░░░░░ ✅ READY    (4/7 Core modules, nie potrzebuje Time)
aquaOneGas     ██░░░░░░░░░░ 🟡 BUILD    (Core Logging działa; pozostałe elementy częściowo TODO)
aquaOneDoser   ███████░░░░░ 🟡 HYBRID   (Core direct + adapters; lokalna domena/MQTT/diagnostyka)
aquaOneClima   ░░░░░░░░░░░░ 🔴 PLAN     (świeży projekt)
aquaOneFauna   ░░░░░░░░░░░░ 🔴 PLAN     (świeży projekt)
aquaOneCore    ███████░░░░░ 🟢 STABLE   (7/7 modułów, 3 planned)
```

---

## Ścieżka dla każdego projektu

### Luma
- Status: ✅ Gotowy — bez zmian, tylko dokumentacja

### Hydro
- Status: 🟡 Prawie gotowy
- TODO: Opcjonalny AquaCore::Logger (low priority)
- Ryzyko: Niskie

### Gas
- Status: 🟡 Architektura OK
- Etapy: 1) Config+Storage (jeśli potrzebne) 2) Network+Web (jeśli potrzebne)
- Ryzyko: Medium

### Doser
- Status: 🟡 Integracja hybrydowa; native Web i streaming OTA CURRENT; G5 bare-board HIL PASS 2026-10-09 (bez pomp i stopni wykonawczych)
- Następny krok: Phase 10 jest IN PROGRESS (F10.1A docs-only); F10.1B poprzedza F10.2, Doser MQTT/Discovery migracja w F10.6; produktowy WS wymaga osobnego zatwierdzonego zakresu
- Ryzyko: Średnie/wysokie; wymagane punkty regresji i testy sprzętowe

### Clima
- Status: 🔴 Nowy projekt
- Integracja: Od razu ze standardem
- Ryzyko: Niskie

### Fauna
- Status: 🔴 Nowy projekt
- Integracja: Od razu ze standardem
- Ryzyko: Niskie

## Doser F9.7F1 gate

F9.7F1/F2 and F9.7G1–G5 are CLOSED. Doser production Web now uses one
`EspIdfWebTransport` + `NativeWebService` listener with native authenticated
streaming `POST /update`. Legacy `WebManager`/`DoserWebRuntime` is not a production
owner. F9.7G5 proved OTA slot switch, exact target image hash, NVS retention and
300 s bare-board runtime stability. Physical pumps/output stages were not connected
or tested. Doser MQTT/Discovery remains LEGACY CURRENT until F10.6. SEC-101
remains OPEN, SYS-107 OPEN/transitional, and common OTA is Phase 11 FUTURE.
Details are in docs/WEB_F9_7G1_DOSER_OTA_DESIGN.md.

## Architecture vNext perspective

Macierz jest snapshotem CURRENT, a nie rankingiem architektury. Wszystkie domeny — Luma,
Doser, Hydro, Clima, Gas i Fauna — są równorzędnymi klientami przyszłej platformy.
Doser, Luma i Hydro nie są wzorcami Core vNext. Dla każdego istniejącego elementu
obowiązuje późniejsza ocena KEEP, ADAPT, REWRITE albo REMOVE.

Docelowe rozszerzenia platformy obejmują Commands, Events, Alarms, Safety,
Maintenance, Registry, produktową kompozycję Realtime, Home Assistant client integration,
wspólne OTA, Backup/Restore, Factory Reset oraz pełną Application lifecycle/composition.
Core Realtime foundation jest CURRENT, bez produkcyjnego WS w Luma/Hydro/Doser.
Phase 9 jest CLOSED po F9.8B; Phase 10 jest IN PROGRESS po F10.1A, bez implementacji integracji.
