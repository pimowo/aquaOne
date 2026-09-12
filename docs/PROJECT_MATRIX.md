# Project Matrix — Status i Integracja Core

**Snapshot date:** 2026-09-12
**Snapshot commit:** `c644201`

Ten dokument opisuje wyłącznie stan zaimplementowany w lokalnym kodzie dla wskazanego
commita. Nie definiuje architektury docelowej ani kolejności przyszłych prac.

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
| **Status** | 🟡 Funkcjonalny, integracja hybrydowa | W1/W1.5 Web DONE; hardware validation PASSED 2026-09-12; evidence not yet persisted in repository |
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
| MQTT/HA | LOCAL | Istniejący PubSubClient + 205 encji nie jest kontraktem przyszłego Core MQTT |
| **Web W1/W1.5** | DONE | Jeden serwer, auth, restart, OTA success/abort/cleanup/reconnect |
| **Następny etap Web** | PLANNED | W2: pozostałe strony/API zgodnie z WEB_STANDARD |
| **Ryzyko dalszej migracji** | Średnie/wysokie | Lokalna domena działa i nie może zostać naruszona |

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
| MQTT | ❌ | Planned (no version assigned) |
| OTA | ❌ | Planned (no version assigned) |
| Home Assistant integration | ❌ | Planned (no version assigned) |
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
