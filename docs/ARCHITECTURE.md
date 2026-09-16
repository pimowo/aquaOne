# Architektura ekosystemu aquaOne

> Architecture vNext (TARGET) opisuje docelową platformę. CURRENT jest wyłącznie inwentaryzacją istniejącego kodu. Legacy code is not architecture. Istniejące elementy będą później oceniane jako KEEP, ADAPT, REWRITE albo REMOVE.

## Hierarchia dokumentacji

Ten dokument definiuje nadrzędne granice i kierunek zależności. Szczegółowe kontrakty są
własnością odpowiednich standardów:

- [Web](WEB_STANDARD.md), [MQTT](MQTT_STANDARD.md),
  [Config i storage](CONFIG_STORAGE_STANDARD.md), [diagnostyka](DIAGNOSTICS_STANDARD.md);
- [alarmy](ALARM_STANDARD.md), [bezpieczeństwo](SAFETY_STANDARD.md),
  [OTA](OTA_STANDARD.md), [testy](TEST_STANDARD.md);
- [nazewnictwo i wersjonowanie](NAMING_VERSIONING_STANDARD.md),
  [factory reset i onboarding](FACTORY_RESET_ONBOARDING_STANDARD.md).
- [decyzje Architecture vNext](ARCHITECTURE_VNEXT_DECISIONS.md).

[PROJECT_MATRIX.md](PROJECT_MATRIX.md) jest snapshotem aktualnej implementacji,
[ROADMAP.md](ROADMAP.md) opisuje przyszłe prace, a [IDEAS.md](IDEAS.md) zawiera wyłącznie
niezatwierdzone pomysły. Standard opisuje wymagany kontrakt; nie jest sam w sobie dowodem,
że funkcja została wdrożona. Stan wdrożenia wynika z PROJECT_MATRIX i kodu.

## Założenie fundamentalne

> Każde urządzenie aquaOne jest autonomiczne i wykonuje swoją funkcję niezależnie. Awaria sieci, innego urządzenia czy centralnego serwera nie powinna zatrzymać podstawowej pracy urządzenia.

Konsekwencje tego założenia:

1. **Brak zależności kodowych** między urządzeniami
2. **Opcjonalność sieciowych modułów** (WiFi, MQTT, Web są dodatkami)
3. **Niezależna logika domenowa** w każdym projekcie
4. **Wspólna infrastruktura techniczna** w Core

## Warstwy architektury

### 1. aquaOneCore — Warstwa techniczna

Biblioteka PlatformIO (`aquaOneCore/`) zawierająca niezależne, opcjonalne moduły:

```
aquaOneCore/
├── System          — Boot status, identity, restart reasons
├── Config          — Persistent storage (CRC, versioning, dual-slot)
├── Time            — RTC (DS3231) + NTP + Europe/Warsaw TZ
├── Network         — WiFi state machine (STA + AP)
├── Web             — HTTP server + routing
├── Logging         — Logger + pluggable sinks
└── Diagnostics     — Health snapshot agregator
```

**Właściwości Core:**

- Zawiera wyłącznie pojęcia techniczne (monotoniczny czas, sieć, magazyn)
- Nigdy nie importuje klas domenowych urządzeń
- Brak wiedzy o LED, pompach, czujnikach, profilach
- Każdy moduł jest opcjonalny — projekty wybierają co potrzebują
- Projekty mogą wybrać które moduły potrzebują

**Backend pattern:**

Core stosuje backend pattern tam, gdzie potrzebna jest separacja od platformy lub sprzętu:

```cpp
// Interface (platform-agnostic)
class NetworkBackend {
    virtual bool beginSta(ssid, password) = 0;
    // ...
};

// Concrete (ESP32-specific)
class Esp32NetworkBackend : public NetworkBackend { /* ... */ };

// Service (uses backend)
class NetworkService {
    NetworkService(NetworkBackend& backend) { /* ... */ }
};
```

### 2. Warstwa aplikacyjna — Urządzenia

Poniższy układ jest **RECOMMENDED STRUCTURE** dla nowych projektów. Istniejące projekty
mogą zachować inną strukturę, jeśli utrzymują kierunek zależności i separację domeny,
sprzętu oraz infrastruktury:

```
aquaOneXxx/
├── platformio.ini
│   └── lib_deps =
│       └── symlink://../aquaOneCore  ← lokalna zależność od Core
├── include/
│   ├── BuildConfig.h        # Piny GPIO (sprzętowe)
│   ├── NetworkSecrets.h     # WiFi secrets
│   ├── XxxConfig.h          # Schemat konfiguracji produktu
│   └── app/XxxApp.h         # Composition root
└── src/
    ├── main.cpp             # Entry point
    ├── app/
    │   └── XxxApp.cpp       # begin(), update(), orchestration
    ├── domain/
    │   └── [business logic]  # Algorytmy, modele, stany
    ├── drivers/
    │   └── [hardware]        # Sterowniki: motor, sensor, LED
    ├── services/
    │   └── [helpers]         # Stateless utils, algorithms
    └── interfaces/
        └── [optional]        # Web, MQTT routes
```

**Separacja warstw:**

- **domain/** — Logika biznesowa, niezależna od hardware
  - Przykład: `TopupController` (kiedy startować pompę)
  - Nie zna GPIO, I2C, SPI

- **drivers/** — Adaptacja hardware
  - Przykład: `Pump` class (control GPIO relay)
  - Znają piny, interfejsy komunikacji

- **services/** — Algorytmy, helpery
  - Przykład: `GasCalculator` (kg CO2 z sensorów)
  - Stateless, pure functions

- **interfaces/** — Pluggable integracje
  - Przykład: `HydroSenseWeb` (HTTP routes)
  - Opcjonalne, można wyłączyć

### 3. Kierunek zależności

```
FirmwareApp
  ├─ AppConfig (local)
  ├─ HardwareInterface (local)
  ├─ DomainLogic (local)
  └─ AquaCore services (optional)
       ├─ NetworkService
       ├─ StorageService
       ├─ RtcService / NtpService
       └─ WebService
```

**Zasada:** Urządzenie zależy od Core, Core nie zależy od urządzeń.

Core może być buildowany, testowany, wydawany **niezależnie** od projektów.

### 4. Komunikacja między warstwami

**Urządzenie wewnętrzne (app → domain → drivers):**
```cpp
class TopupController {
    void update(WaterLevel level, uint32_t nowMs) {
        if (level.isLow()) {
            pump_.start();  // Mówi pump driver co robić
        }
    }
};
```

**Urządzenie → Core (app orchestration):**
```cpp
void XxxApp::begin() {
    // Core modules are init'd w XxxApp
    storage_.begin(...);
    network_.begin(config);
    web_.begin(...);
}

void XxxApp::update() {
    network_.update(millis());  // Update state machines
    web_.update();
}
```

**Urządzenie → Urządzenie:** NIE ISTNIEJE
- Jeśli potrzebna jest wymiana danych, to przez MQTT (przyszłość)
- Lub przez wspólną bazę konfiguracji (przyszłość)

### 5. Optional Core modules

Projekt wybiera które moduły potrzebuje:

```cpp
// aquaOneHydro nie ma timera, nie inicjalizuje Time
// aquaOneHydro::HydroSenseApp::begin() {
//     // Brak RTC/NTP inicjalizacji
//     networkService_.begin(config);  // ← Network: TAK
//     webService_.begin(config);      // ← Web: TAK
// }

// aquaOneLuma potrzebuje czasu dla profili świetlnych
// LumaSense::FirmwareApp::begin() {
//     timeService_.begin();           // ← Time: TAK
//     networkService_.begin(config);  // ← Network: TAK
// }

// aquaOneGas — celów będzie potrzebne Storage i Config
// gassense::GasSenseApp::begin() {
//     configStorage_.begin();         // ← Config: TAK
//     // Pozostałe moduły: TBD
// }
```

Moduły Core NIE inicjalizują się automatycznie. App musi je uruchomić w `begin()`.

### 5.1. Własność transportu HTTP i polityki produktu

W jednym firmware działa dokładnie jeden fizyczny serwer HTTP. `AquaCore::Web::Esp32WebBackend`
jest właścicielem instancji Arduino `WebServer`, a `WebService` udostępnia neutralny transport,
routing, request/response, nagłówki, Basic Auth i lifecycle uploadu. Kod produktu nie tworzy
drugiego serwera i rejestruje własne trasy na tej samej usłudze.

Core nie podejmuje decyzji domenowych. W Doserze polityka restartu i OTA pozostaje w
`WebManager`: autoryzacja, zatrzymanie pomp, maintenance, zapis firmware, cleanup po błędzie
i opóźniony restart są efektami produktu wykonywanymi przez `WebManagerRuntime`.

Etapy integracji Web:

- **W1 — DONE:** neutralny transport AquaCore Web i pojedynczy backend ESP32;
- **W1.5 — DONE:** autoryzowany restart i OTA Dosera na wspólnym serwerze, w tym success,
    abort, cleanup i reconnect. Hardware validation: **PASSED** 2026-09-12. Evidence: not yet
    persisted in repository;
- **W2 — NEXT:** migracja pozostałych stron i API produktu zgodnie z
    [WEB_STANDARD.md](WEB_STANDARD.md).

W1.5 nie obejmuje MQTT ani zmiany kontraktu Home Assistant.

### 6. EuropeWarsawTimeService — Specjalizacja

```cpp
// W Core (AquaCore::Time::EuropeWarsawTimeService)
class EuropeWarsawTimeService {
    LocalTime now() {
        LocalTime utc = rtc_.read();
        return convertUtcToLocal(utc);  // Applying DST rules for Europe/Warsaw
    }
};
```

**Dlaczego w Core?**
- aquaOne jest obecnie przeznaczony dla Polski
- DST rules (zmiana czasu) są wspólne dla wszystkich urządzeń
- To nie jest adapter urządzenia — to są reguły geograficzne

## Bezpieczeństwo i Niezawodność

### Zależy od sprzętu (nie od sieci)

Funkcja urządzenia **nigdy** nie powinna zależeć od:
- WiFi connection
- MQTT broker
- Home Assistant
- innego urządzenia aquaOne
- Cloud API

Przykład Luma:
- ✅ Światło zmienia się wg profilu (lokalnie, nie potrzebuje sieci)
- ⚠️ Web panel niedostępny, jeśli brak WiFi (OK — to addon)
- ⚠️ MQTT disconnect (OK — to addon, przyszłość)

### State machines i recovery

Moduły wymagające obsługi stanów, retry i recovery mogą używać state machine:

```cpp
// NetworkService
enum class NetworkState {
    Disabled,
    Idle,
    Connecting,
    Connected,
    Disconnected,
    Error
};
```

State machine jest **resilient**:
- Retry logic wbudowany
- Timeout handling (nie utknie czekając)
- Graceful degradation (działa bez sieci)

### Logging i diagnostyka

```cpp
// Wbudowane logowanie (opcjonalne)
AquaCore::Logger logger;
logger.info("module", "message");

// Snapshot diagnostyki
Diagnostics::DiagnosticsSnapshot snapshot = diagnostics_.snapshot();
// snapshot.network.state
// snapshot.time.rtcValid
// snapshot.storage.hasValidPayload
```

Zawsze można sprawdzić `status()` bez side effects.

## Wersjonowanie i kompatybilność

Projekty mają własne wersje firmware, ale bieżące implementacje nie muszą przechowywać ich
w tym samym typie pliku. Przykładowo Luma definiuje wersję firmware w `include/Version.h`,
a Core ma wersję biblioteki w `aquaOneCore/library.json`.

Zasady nazw, niezależnych wersji firmware/Core/schema/protocol oraz kompatybilności definiuje
[NAMING_VERSIONING_STANDARD.md](NAMING_VERSIONING_STANDARD.md).

Projekty w bieżącym monorepo używają lokalnej zależności:

```text
lib_deps =
    symlink://../aquaOneCore
```

Wszystkie korzystają z tego samego checkoutu Core. Repozytorium nie ma obecnie mechanizmu
niezależnego przypinania wersji Core per projekt; reproducible per-project pinning pozostaje
osobnym przyszłym problemem i nie jest deklarowane jako dostępna funkcja.

## Design decisions (i ich uzasadnienie)

| Decision | Uzasadnienie |
|----------|-------------|
| Autonomia > sieć | System powinien działać bez zależności |
| Backend pattern dla sieciowych/sprzętowych modułów | Portability, testability, niezależność Core od ESP32 |
| Optional modules | Lekkie urządzenia (np. Hydro) nie muszą mieć RTC |
| Dual-slot storage | Atomic updates, safety |
| State machines dla modułów sieciowych/recovery | Predictable, debuggable, resilient |
| Logging pluggable | Można wyłączyć w release, zmienić sink |
| No config inheritance | Każde urządzenie ma swoją konfigurację |
| Brak centralnego "orchestrator" | Każde urządzenie orchestruje siebie |
| Jeden fizyczny serwer HTTP | Core posiada transport; urządzenie posiada trasy i politykę efektów |

## Architecture vNext — TARGET

Poniższe zasady są docelowym kontraktem platformy. Nie opisują stanu bieżącej implementacji.

### Autonomia i granice

Każde urządzenie aquaOne MUSI działać autonomicznie. Podstawowa funkcja domenowa nie może zależeć od Home Assistant, MQTT, WWW/HTTP, WebSocket/realtime, aquaOne Panel, Internetu ani innego urządzenia aquaOne. Awaria komunikacji nie może zatrzymać podstawowej funkcji domenowej.

Core dostarcza mechanizmy wspólne, a Domain dostarcza znaczenie funkcjonalne. Core może przechowywać `device_type` jako metadane, ale nie może zawierać logiki `if DOSER`, `if LUMA`, `if HYDRO` ani równoważnych rozgałęzień produktowych. Luma, Doser, Hydro, Clima, Gas i Fauna są równorzędnymi klientami platformy. Panel jest klientem/interfejsem, nie zależnością krytyczną domeny.

### Warstwy i zależności

```text
Application / Composition Root
    ├── aquaOneCore
    ├── Domain
    ├── Application adapters
    └── Hardware adapters / drivers
```

Domain korzysta z wąskich kontraktów Core i domenowych interfejsów hardware, ale nie zna transportów. Composition Root jest jedynym miejscem znającym konkretny skład urządzenia, BoardProfile i połączenia usług. Domain nie otrzymuje całego `AquaOneCore&`; zależności są jawne, preferowana jest dependency injection. Registry jest rejestrem capability/providerów, nie service locatorem.

Composition Root współpracuje z `ApplicationRuntime`, który koordynuje startup i runtime
wyłącznie przez wąskie kontrakty. ApplicationRuntime nie posiada konkretnych usług, nie zna
konkretnych klas Domain, Network, Web ani MQTT, nie jest service locatorem ani Registry i nie
zawiera semantyki domenowej. Dokładny ApplicationPlan i hooks pozostają DECISION REQUIRED.

Domain odpowiada za state, config domenowy, logic, commands, domain mode, domain status, alarms, safety policy i diagnostics domenowe. Domain nie używa bezpośrednio WiFi, WebServer, WebSocket, PubSubClient, Preferences/NVS, Update, `ESP.restart()` ani przypadkowych GPIO. Hardware jest dostępny przez jawne interfejsy domenowe, np. `IDosingPump`, `ILightOutput`, `IReservoirLevelSensor`.

Docelowy Core obejmuje Identity, Lifecycle, System state, Time, Storage, Config framework, Logging, Diagnostics, Alarm framework, Safety framework, Maintenance, Network, Commands, Events, Registry, Web, Realtime, MQTT infrastructure, OTA, Restart, Factory Reset, Backup/Restore, Versioning i Auth. To jest TARGET; nie wszystkie elementy są CURRENT.

### Lifecycle i model stanu

```text
POWER ON → BOOT → CORE INIT → LOAD/VALIDATE CONFIG → HARDWARE INIT →
DOMAIN INIT → SAFETY VALIDATION → NETWORK INIT → INTERFACES INIT → RUNNING
```

Publiczny `StartupPhase` obejmuje `BOOT`, `CORE_INIT`, `LOAD_VALIDATE_CONFIG`,
`HARDWARE_INIT`, `DOMAIN_INIT`, `SAFETY_VALIDATION`, `NETWORK_INIT`, `INTERFACES_INIT` i
`RUNNING`. Nie opisuje Maintenance, Recovery ani Restart. `EarlySafeOutputInitializer`
wykonuje idempotentne ustawienie konserwatywnego stanu fizycznych wyjść na początku `BOOT`,
przed configiem i pełnym hardware init; jest mechanizmem technicznym, nie częścią Safety
framework.

Sieć i integracje są opcjonalne i nie są warunkiem gotowości ani działania domeny.

Startup rozdziela `StartupRequirement` (`REQUIRED`, `OPTIONAL`) od `StartupOutcome`
(`SUCCEEDED`, `DISABLED`, `FAILED`). Optional disabled nie degraduje health, optional failed
może dać `DEGRADED`, a required failed zatrzymuje normalny startup jako
`ERROR + FAULT + LOCKED`. `REQUIRED + DISABLED` jest nieprawidłowe. Każdy startup failure
posiada stabilny, krótki error code; dokładny katalog i reprezentacja pozostają DECISION
REQUIRED.

Nie powstaje jeden ogromny enum. Rozdzielone są `OperationalState` (`BOOTING`, `RUNNING`, `MAINTENANCE`, `ERROR`), `HealthState` (`OK`, `DEGRADED`, `FAULT`) i `SafetyState` (`CLEAR`, `LOCKED`), a niezależnie istnieją `DomainMode`, aktywne alarmy i Action Locks. `RUNNING + DEGRADED + CLEAR` jest prawidłową kombinacją. `ERROR` oznacza poważny stan systemowy uniemożliwiający normalną pracę.

W Fazie 1 ApplicationRuntime jest authoritative writer dla OperationalState, HealthState
wynika z minimalnej koordynacji startup/runtime, a SafetyState startuje jako `LOCKED` i może
zostać ustawiony na `CLEAR` przez startup safety gate. Nie powstaje ogólne
`setSafetyState()`. Workflow `MAINTENANCE`, SafetyManager i Action Locks należą do
późniejszych faz.

Identity jest rozdzielone na `DeviceIdentity`, `BuildIdentity`, `HardwareIdentity` i
`RuntimeIdentity`; friendly name nie należy do technical identity. `RestartReason`,
`RestartRequestReason` i `RestartExecutor` są osobnymi pojęciami. Domain i transport mogą
otrzymać tylko `RestartRequester`, a minimalny restart Fazy 1 przechodzi przez pending request
i safe point runtime loop. Dokładne pola identity, runtime identity oraz rozszerzone workflow
restartu pozostają DECISION REQUIRED.

### Command Path

Wszystkie źródła sterowania — WEB, MQTT, PANEL, przycisk fizyczny, scheduler, automatyka lokalna i system — korzystają z jednej ścieżki:

```text
Source → Command → Validation → Authorization/Policy → Safety/Action Locks →
Domain execution → State update → Event/Result
```

Transport nigdy nie steruje bezpośrednio GPIO/driverem. Scheduler również korzysta z Command Path. Command i Event są osobnymi pojęciami. Długie operacje mogą używać `operation_id`; dokładne envelope, error codes i idempotency pozostają DECISION REQUIRED.

### Snapshot, Events i Realtime

Rozróżniamy Snapshot, State Change, Domain Event, Alarm Event, Operation Event i Telemetry. Snapshot jest autorytatywnym źródłem aktualnego stanu, Event mówi, że coś się wydarzyło. Realtime nie jest jedynym źródłem prawdy. W V1 nie ma event replay; po reconnect klient zawsze wykonuje pełny resync. `boot_id`/`runtime_id`, sekwencje, gap detection i nazwy stanów UI pozostają DECISION REQUIRED.

### Config, Storage, Safety i Alarmy

Rozdzielone są `CoreConfig`, `DomainConfig`, `DomainState`, `SystemState` i `RuntimeState`; RuntimeState nie jest persistent. Config lifecycle to `load → decode → version → migrate → validate → apply`. Walidacja następuje przed zapisem i zastosowaniem, a migracje są jawne (`v1 → v2 → v3`). Obecny StorageService ma wartościowe cechy i jest kandydatem do KEEP.

Maintenance, Safety i Action Lock są trzema różnymi mechanizmami. Maintenance opisuje stan serwisowy, Safety chroni system, a Action Lock blokuje konkretną akcję. Jedna akcja może mieć wiele powodów blokady. STOP, EMERGENCY_STOP, status, diagnostics i ACK nie są automatycznie blokowane globalną blokadą. Warning, alarm, fault i safety lock są odrębne; alarm nie oznacza automatycznie Safety Lock, a ACK nie oznacza CLEAR.

### Hardware, Web i Security

Core nie jest katalogiem konkretnych driverów. Rozróżniamy generic technical abstractions, concrete hardware drivers i domain hardware interfaces. BoardProfile należy do projektu, a testy domenowe używają fake domain capability, nie fake GPIO.

Na urządzeniu pozostaje dokładnie jeden fizyczny transport Web. HTTP i Realtime mają docelowo korzystać ze wspólnego backendu i portu; nie wolno tworzyć konkurencyjnego WebServera. Obecny synchroniczny Web Core jest CURRENT/legacy foundation. Provider pattern jest wartościowy, ale jego obecne API nie jest gwarantowanym kontraktem bez breaking changes. HTTP obsługuje request/response, initial/full snapshot, konfigurację, akcje, OTA i backup/restore; Realtime obsługuje live state, events, alarms, warnings i progress.

Auth odpowiada „kto może wykonać akcję”, Safety „czy akcja może być teraz wykonana”. Auth należy do Core; Domain nie zna haseł, sesji, nagłówków HTTP ani WebSocket handshake auth. Sekrety nie trafiają do status, diagnostics, logs, realtime ani MQTT state. Logging opisuje, co się wydarzyło, diagnostics opisuje stan obecny. `CoreDiagnostics` i `DomainDiagnostics` są semantycznie oddzielone.

### CURRENT → vNext gap

| Obszar | Ocena vNext |
|---|---|
| StorageService | KEEP jako mechanizm bazowy |
| Config jako pełny subsystem | ADAPT / SPLIT |
| Logging, System/Identity, Time, Network | ADAPT |
| Diagnostics | REWRITE architektoniczne na provider/capability |
| Web | REWRITE architektoniczne; najpierw feasibility spike |
| Commands, Events, Alarms, Safety, Maintenance, Realtime | BUILD NEW |
| MQTT, OTA, Backup/Restore, Factory Reset, Registry, lifecycle | BUILD NEW |

Istniejące projekty nie są wzorcem platformy. Każdy projekt może później zostać oceniony jako KEEP, ADAPT, REWRITE albo REMOVE.
