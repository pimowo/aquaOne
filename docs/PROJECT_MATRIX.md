# Project Matrix — Status i Integracja Core

## Legenda

- 🟢 **Gotowy** — Zaimplementowany, testowany
- 🟡 **W budowie** — Funkcjonalny, wymaga integracji
- 🔴 **Planowany** — Szkielet, brak implementacji
- ✅ **READY** — Core module confirmed stable
- ⚠️ **PARTIAL** — Core module partial integration
- ❌ **NOT USED** — Device doesn't use this module
- 〰️ **NOT NEEDED** — Device doesn't require this module

## aquaOneLuma — 🟢 Lighting Controller (Stabilny)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Platforma** | ESP32 (AQMA, LOLIN32 testowa) | |
| **Status** | 🟢 Zweryfikowany | Pierwszy projekt, referencyjny |
| **Architektura** | app/core/hardware/web/time/storage | Wzorzec do naśladowania |
| | | |
| **Używane moduły Core** | | |
| System | ✅ READY | systemService (boot, uptime) |
| Config | ✅ READY | StorageService (dual-slot, CRC32) |
| Logging | ✅ READY | Logger + SerialLogSink |
| Diagnostics | ✅ READY | DiagnosticsService agregator |
| Network | ✅ READY | NetworkService (WiFi state machine) |
| Web | ✅ READY | WebService + provider pattern |
| Time | ✅ READY | RtcService, NtpService, EuropeWarsawTimeService |
| | | |
| **Elementy lokalne** | | |
| Logika | LightEngine, TransitionEngine, DayEngine, ModeManager | |
| Konfiguracja | DeviceConfig, ChannelConfig, profiles | |
| Hardware | PWM LED drivers, AqmaHardware, Lolin32Hardware | |
| Web UI | LumaPages, LumaApi, profil editor | |
| | | |
| **Migracja do Core** | — | Już zintegrowany, bez zmian |
| **Ryzyko** | ✅ Niskie | Stabilna architektura |

---

## aquaOneDoser — 🟡 Nutrient Dispenser (Funkcjonalny)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Platforma** | ESP32-S3 Super Mini (4MB) | |
| **Status** | 🟡 Funkcjonalny | Działający, ale zaisolowany od Core |
| **Architektura** | Flat (11 managerów na 1 poziomie) | Wymaga refaktoringu |
| | | |
| **Używane moduły Core** | | |
| System | ❌ NOT USED | Brak integracji |
| Config | ❌ NOT USED | Własny StorageManager (raw Preferences) |
| Logging | ❌ NOT USED | Serial.println() |
| Diagnostics | ❌ NOT USED | Własny DiagnosticsManager |
| Network | ❌ NOT USED | Własny WiFiManager (event loop) |
| Web | ❌ NOT USED | Własny WebManager (WebServer) |
| Time | ❌ NOT USED | Własny TimeManager (RTClib + esp_sntp) |
| | | |
| **Elementy lokalne** | | |
| Logika | PumpManager, SchedulerManager, MqttManager, HaDiscovery | |
| Konfiguracja | PumpConfig (array[4]) | |
| Hardware | Relay drivers, PWM pump control | |
| | | |
| **Różnice vs Core** | | |
| WiFi | WiFiManager (custom SM) vs NetworkService (formal SM) | |
| Storage | Manual Preferences vs StorageService (CRC+versioning) | |
| Time | TimeManager (recovery logic) vs RtcService (plain read) | |
| | | |
| **Migracja do Core** | 🟡 PLANNED | Wieloetapowa migracja |
| **Skalowanie ryzyka** | Wysoki | Duża zmiana, wiele zależności |

---

## aquaOneHydro — 🟢 Water Topup & Level Control (Funkcjonalny)

| Aspekt | Status | Notatki |
|--------|--------|---------|
| **Platforma** | ESP32-S3 DevKit-C1 | |
| **Status** | 🟢 Funkcjonalny | Dobra architektura, przystosowana na Core |
| **Architektura** | app/hardware/hydrosense/web | Czysta separacja |
| | | |
| **Używane moduły Core** | | |
| System | ✅ READY | systemService (boot info) |
| Config | ✅ READY | HydroSenseConfigStorage adapter |
| Logging | ❌ NOT USED | Brak logowania |
| Diagnostics | ❌ NOT USED | HydroSenseDiagnosticsPage (local) |
| Network | ✅ READY | NetworkService (WiFi) |
| Web | ✅ READY | WebService + custom providers |
| Time | 〰️ NOT NEEDED | Brak wymagań czasowych |
| | | |
| **Elementy lokalne** | | |
| Logika | TopupController, WaterTank, AlarmManager | Domena: zawór+czujniki |
| Hardware | Pump, FloatSensor, UltrasonicSensor, Button, Buzzer | |
| Web UI | Dashboard, SettingsPage, ControlPage | |
| | | |
| **Brakujące integracje** | | |
| Logging | Opcjonalnie można dodać AquaCore::Logger | Not priority |
| MQTT | Nie planowany teraz | — |
| | | |
| **Migracja do Core** | 🟢 READY | Już dobra architektura |
| **Ryzyko** | ✅ Niskie | Proste dodatki (Logger) |

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
| Logging | ⚠️ PARTIAL | Imports tylko, nie faktycznie używa |
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
| Etap 3 | MQTT (przyszłość) | HA integration |
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
| **Status** | 🟢 Stabilna | Używana (Luma), integrowana (Hydro) |
| **Struktura** | include/AquaCore + src/AquaCore | |
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
| MQTT | ❌ | Planned (no version assigned) |
| OTA | ❌ | Planned (no version assigned) |
| Home Assistant integration | ❌ | Planned (no version assigned) |
| RTC health monitoring | ⚠️ CANDIDATE | Optional module, backlog |
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
| Budowa | Gas (prep) | Imports tylko, TBD |
| Planowanie | Doser, Clima, Fauna | Migracja / green projects |
| | | |
| **Rozwój** | — | Core rozwija się niezależnie; projekty integrują wybrane moduły Core |

---

## Podsumowanie — Integracja vs Status

```
aquaOneLuma    ████████████ ✅ READY    (7/7 Core modules)
aquaOneHydro   ████░░░░░░░░ ✅ READY    (4/7 Core modules, nie potrzebuje Time)
aquaOneGas     ██░░░░░░░░░░ 🟡 PREP     (architektura OK, integracja TBD)
aquaOneDoser   ██░░░░░░░░░░ 🟡 TODO     (dużo reimpl, migracja planned)
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
- Status: 🔴 Pełna migracja potrzebna
- Strategy: Etapowo, reverse-commit gotowe
- Ryzyko: High

### Clima
- Status: 🔴 Nowy projekt
- Integracja: Od razu ze standardem
- Ryzyko: Niskie

### Fauna
- Status: 🔴 Nowy projekt
- Integracja: Od razu ze standardem
- Ryzyko: Niskie
