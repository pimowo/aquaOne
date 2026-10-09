# Project Matrix — Status i Integracja Core

**Snapshot date:** 2026-10-09
**Snapshot base commit:** e93c00f plus local F9.7G1 design candidate

Ten dokument opisuje wyłącznie stan zaimplementowany w lokalnym kodzie dla wskazanego
commita. Nie definiuje architektury docelowej ani kolejności przyszłych prac.

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
| **Status** | 🟡 F9.7F2 CLOSED, integracja hybrydowa | Native Web foundation tested; one legacy production Web owner remains; F9.7G1 design accepted |
| **Architektura** | Composition root + lokalne managery/adapters | Migracja Core jest częściowa |
| | | |
| **Używane moduły Core** | | |
| System | CORE DIRECT | `SystemService`, `DeviceIdentity` |
| Config | CORE VIA ADAPTER | `StorageManager` używa dwóch `StorageService`; obsługuje też migrację legacy Preferences |
| Logging | CORE DIRECT | `Logger` + `SerialLogSink`; lokalne logi `Serial` nadal istnieją |
| Diagnostics | LOCAL | Lokalny `DiagnosticsManager` |
| Network | CORE VIA ADAPTER | Lokalny `WiFiManager` deleguje do `NetworkService` i `Esp32NetworkBackend` |
| Web | CORE DIRECT | Jeden `Esp32WebBackend` i `WebService`; lokalny `WebManager` posiada politykę restart/OTA |
| Time | CORE VIA ADAPTER | Lokalny `TimeManager` komponuje usługi RTC/NTP/resilient time Core |
| | | |
| **Elementy lokalne** | | |
| Logika | PumpManager, SchedulerManager, MqttManager, HaDiscovery | |
| Konfiguracja | `PumpConfig`, dokładnie 8 pomp | |
| Hardware | Relay drivers, PWM pump control | |
| MQTT/HA | LEGACY CURRENT / LOCAL | Istniejące PubSubClient, MqttManager, HaDiscovery i około 205 encji pozostają do migracji Dosera; nie są TARGET Core ani docelowym modelem encji HA |
| **Web W1/W1.5** | DONE | Jeden serwer, auth, restart, OTA success/abort/cleanup/reconnect |
| **Następny etap Web** | F9.7G2 REQUIRED NEXT | Core neutral streaming foundation; F9.7G3–G5 complete OTA and atomic production cutover |
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
| **Status** | 🟢 CURRENT legacy foundation | Używana (Luma), integrowana (Hydro); nie jest jeszcze pełną platformą vNext |
| **Struktura** | `include/AquaCore/<Module>/` + `src/<Module>/` | Implementacje znajdują się bezpośrednio pod `aquaOneCore/src/` |
| | | |
| **Moduły główne** | | |
| System | ✅ READY | SystemService, DeviceIdentity, RestartReason |
| Config | ✅ READY | StorageService (CRC32, versioning, dual-slot) |
| Logging | ✅ READY | Logger + SerialLogSink + compile-time control |
| Diagnostics | ✅ READY | DiagnosticsService (snapshot agregator) |
| Network | ✅ READY | NetworkService + Esp32NetworkBackend |
| Web | ✅ READY | WebService + Esp32WebBackend + routing |
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
- Status: 🟡 Integracja hybrydowa; W1/W1.5 Web DONE; hardware validation PASSED 2026-09-12;
	evidence not yet persisted in repository
- Następny krok: W2 Web, bez równoległego przepisywania domeny lub MQTT
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

Doser Web is LEGACY CURRENT: one `Esp32WebBackend` + `WebService` +
`WebManager` owns restart and authenticated multipart OTA. F9.7F1 and F9.7F2
are CLOSED: the native foundation, Basic Auth boundary, projections and restart
bridge are implemented and tested, while legacy remains the sole production owner.
F9.7G must provide streaming OTA before one atomic native production cutover.
Doser MQTT/Discovery remains LEGACY CURRENT until F10.6.

F9.7G1 is an accepted docs-only design gate. The streaming transport, Doser-local
multipart parser, Application-owned Update lifecycle and atomic cutover remain
unimplemented; details are in docs/WEB_F9_7G1_DOSER_OTA_DESIGN.md.

## Architecture vNext perspective

Macierz jest snapshotem CURRENT, a nie rankingiem architektury. Wszystkie domeny — Luma,
Doser, Hydro, Clima, Gas i Fauna — są równorzędnymi klientami przyszłej platformy.
Doser, Luma i Hydro nie są wzorcami Core vNext. Dla każdego istniejącego elementu
obowiązuje późniejsza ocena KEEP, ADAPT, REWRITE albo REMOVE.

Docelowe, jeszcze nie CURRENT, obszary platformy to Commands, Events, Alarms, Safety,
Maintenance, Realtime, Registry, Home Assistant client integration, OTA, Backup/Restore, Factory Reset oraz
Application lifecycle/composition.
