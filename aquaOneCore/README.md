# aquaOneCore v0.6.2

Wspólna biblioteka technicznych usług dla ekosystemu aquaOne.

Core dostarcza abstrakcje dla funkcji wspólnych do wszystkich urządzeń — boot, persistent storage, czas (RTC + NTP), sieć (WiFi), web server, logging, diagnostyka. Nie zawiera logiki domenowej (oświetlenie, pompy, czujniki, algorytmy).

## Status: CURRENT foundation (v0.6.2)

- 7 modułów zaimplementowanych i weryfikowanych
- Używana przez Luma i Hydro; Doser ma integrację hybrydową; Gas używa Core Logging
- Publiczne API opisuje bieżący baseline; Architecture vNext może wymagać breaking changes

## CURRENT i TARGET

Ten README opisuje CURRENT aquaOneCore v0.6.2: używaną bibliotekę technicznych usług
System, Config/Storage, Logging, Diagnostics, Network, Web i Time. CURRENT nie jest jeszcze
pełną platformą Architecture vNext.

Architecture vNext jest TARGET. Docelowo Core obejmie również lifecycle/composition,
Commands, Events, Alarms, Safety, Maintenance, Registry, Realtime, wspólny MQTT, OTA,
Backup/Restore, Factory Reset, Auth i wersjonowanie. Te elementy nie są automatycznie
zaimplementowane tylko dlatego, że występują w architekturze docelowej.

Istniejący kod i API mogą zostać ocenione jako KEEP, ADAPT, REWRITE albo REMOVE. Ten README
zachowuje dokumentację CURRENT API, ale nie obiecuje braku breaking changes ani zgodności
z przyszłym Core vNext.

**CURRENT F3.1/F3.2 — Command foundation:** `AquaCore::Commands::DomainCommandResult`
udostępnia transport-neutralne wyniki wykonania dopuszczonej komendy domenowej: `Completed`,
`Rejected`, `InvalidState` i `OperationStarted`, wraz ze stabilnym helperem nazw. Foundation
obejmuje też typed `CommandPipeline<T>` wykonujący validation → policy → safety gate → handler,
z short-circuit i oddzielnym `CommandExecutionResult`. Wszystkie callbacki są wymagane i
statycznie komponowane; borrowed context może być `nullptr` dla callbacka bezstanowego. Brak
callbacka jest fail-closed. Obecny safety gate jest wąskim
punktem orkiestracji, nie SafetyManagerem. F3.4 dodaje oddzielny Action Lock foundation; jego integracja jako gate/policy input pozostaje poza tym krokiem. Foundation nie implementuje routingu, envelope, source metadata, identyfikatorów żądań ani async operation API.

## 📦 Moduły (v0.6.2)

### System — ✅ READY

Boot status, device identity, restart reasons.

**CURRENT F1.3:** `AquaCore/System/Identity.h` udostępnia neutralne value types
`AquaCore::Identity::DeviceIdentity` (deviceType), `BuildIdentity` (firmwareVersion,
coreVersion) i `HardwareIdentity` (hardwareVariant). Są to minimalne dane oparte na
CURRENT; legacy `AquaCore::DeviceIdentity`, SystemService i konsumenci pozostają bez migracji.
`assign()` zwraca jawny ValidationResult (error + field); null, pusty tekst lub przekroczenie
limitu czyści cały obiekt i ustawia invalid. Brak truncation i dynamic allocation.
Limity implementacji, łącznie z NUL: deviceType/firmwareVersion 24, coreVersion 16,
hardwareVariant 32 bajty. Istniejące typy i foundation pozostają CURRENT i nie implementują
docelowego DeviceIdentity contract.
Format device_id, MAC/MAC6, finalne pola, capacities i walidacja są opisane przez zaakceptowany kierunek IDN-101.
Statusy kontraktów docelowych: SYS-101 — ACCEPTED — TARGET; IDN-101 — ACCEPTED — TARGET.
**CURRENT F4.2:** `RuntimeIdentity` ma 64-bitową niezerową wartość i zapis jako 16 wielkich
cyfr hex w buforze wywołującego. `RuntimeIdentityState` przechowuje jedną wartość na instancję
runtime i podejmuje generację tylko raz. `RuntimeIdentityStartup` dostarcza wymagany participant
`CORE_INIT`, który Composition Root umieszcza jako pierwszy w tej fazie. ESP32 generator
korzysta z krótkiego okna entropy potwierdzanego przez Composition Root, bez zależności od
Network, czasu ani storage. Adapter nie jest jeszcze podłączony do domen ani EventSink;
integracja planu startupu należy do Composition Root.
Nowe typy są testowane w `platformio test -e native -f test_system`.

**CURRENT F1.4:** `AquaCore/System/SystemState.h` defines independent neutral
`AquaCore::System::OperationalState`, `HealthState`, `SafetyState` and
`StartupPhase` contracts with deterministic name helpers. It does not implement lifecycle
or state transitions. Legacy `AquaCore::SystemState` and `SystemStatus` remain unchanged.

**CURRENT F1.7:** neutralny `AquaCore::System::ApplicationRuntime` implementuje host-tested
startup foundation, plan participantów, state transitions oraz immutable startup report bez
heap. Foundation nie jest jeszcze używana przez domeny i nie obejmuje recovery ani runtime
loop. Legacy `SystemService` pozostaje bez zmian.

**CURRENT F3.3:** `RuntimeStateCoordinator` agreguje statycznie skomponowanych
`HealthProvider` i `SafetyProvider` przez jawny pull `refresh()`. Po udanym startupie
`ApplicationRuntime::handoffRuntimeState()` przekazuje jednorazowo write authority do
Health/Safety bez tworzenia drugiej kopii `RuntimeStatus`; OperationalState i StartupPhase
pozostają własnością runtime. Właściciel condition musi utrzymać startup-derived fact w providerze
niezależnie od pojemności `StartupReport`; gdy aggregate podczas handoffu zgubi startup
`DEGRADED`, runtime przechodzi do `ERROR + FAULT + LOCKED` bez aktywacji coordinatora. Foundation nie implementuje SafetyManagera, alarmów ani integracji CommandPipeline.

**CURRENT F3.4 — Action Lock foundation:** AquaCore/Safety/ActionLockCoordinator.h udostępnia
typed ActionLockProvider<Action> i statyczny pull coordinator z immutable borrowed listą
providerów. Agreguje niezależne blokady przez OR, raportuje safety-critical lock i liczbę
aktywnych providerów; nie utrzymuje reason registry i nie zapisuje SafetyState.
Integrację z CommandPipeline opisuje F3.5 poniżej. Action ID i provider należą do Domain/projektu;
Composition Root posiada providerów oraz listę. Nie jest to pełny Safety framework ani Alarm/
Maintenance implementation.

**CURRENT F3.5/F3.6 — normal Domain command gates:** `CommandClass` rozróżnia
`NormalDomain` i `SystemRecovery` niezależnie od transportu, roli, action ID i wyniku.
`RuntimeCommandPolicyGate` czyta live `RuntimeStatus` i typed class resolver:
dla `NormalDomain` dopuszcza tylko RUNNING, a dla `SystemRecovery` RUNNING i ERROR.
Każda instancja bramki dopuszcza tylko klasę przypisaną do swojej ścieżki handlera.
BOOTING i MAINTENANCE są blokowane w obu klasach (baseline do Phase 5).
Brak readera/resolvera, błędny odczyt lub nieznana klasa blokują policy
(`BlockedByPolicy`). `RuntimeCommandSafetyGate` pozostaje tylko dla normalnych
Domain commands: sprawdza SafetyState CLEAR i odblokowaną typed Action przez
ActionLockCoordinator; odmowa daje `BlockedBySafety`. Obie bramki są potrzebne
w normalnym Domain pipeline; HealthState sam nie blokuje. Po SYS-106 handoff
każde wywołanie czyta świeży stan, bez cache. ERROR recovery shell nie uruchamia
normalnego Domain handlera ani Domain processing. F3.6 określa jedynie policy
dla `SystemRecovery`: per-command safety exceptions, handler/result i konkretne
workflow (w tym restart/config repair) wymagają dalszego kontraktu. Autonomous
Domain control nadal pozostaje poza external command pipeline.

**CURRENT F4.1 — typed semantic event emission:** `AquaCore::Events::EventSink<Event>`
ustanawia kierunek Domain → Application przez wymagany borrowed sink. Typ eventu należy do
Domain; `emit(const Event&)` jest synchronicznym przekazaniem best-effort bez gwarancji
dostarczenia. Composition Root posiada sink i może jawnie użyć `NullEventSink<Event>`.
Foundation nie alokuje heap i nie definiuje globalnego envelope, metadata RuntimeIdentity,
sequence ani transportu. EVT-101 i EVT-102 pozostają otwarte w pozostałym zakresie.

**CURRENT F4.3 — event metadata:** `EventMetadata` zawiera tylko `RuntimeIdentity` oraz
64-bitową `EventSequence`. Application-owned `EventMetadataSequencer` wydaje numery od 1
w jednym runtime stream; po `UINT64_MAX` zwraca `Exhausted` bez wrap. Stan nie jest
utrwalany, a foundation nie zapewnia replay. Domain `EventSink<Event>` nadal przyjmuje
semantic event bez metadata. Wydawanie numerów wymaga serializowanego kontekstu Application;
metadata stamping należy do Application-side processing, a concurrent/ISR issuance pozostaje
poza kontraktem. Realtime może później używać zmiany runtime ID/gap jako sygnałów spójności;
snapshot pozostaje autorytatywny, bez replay/resync w F4.3. EVT-101 i EVT-102 pozostają otwarte.

**CURRENT F4.4 — alarm state foundation:** `AlarmState<AlarmId>` przechowuje ulotny stan
pojedynczego alarmu z typed ID należącym do Domain. Rozróżnia condition od aktywności alarmu,
ACK od clear i opcjonalny latch wybrany przy konstrukcji. Nie mapuje alarmu na Health/Safety
i nie emituje events; transport i pozostały zakres ALM-101 są nadal otwarte.

**CURRENT F4.5 — alarm control i restart:** `AlarmPersistentState` zawiera tylko bit
`latchedPending`. Application odpowiada za jego zapis i jedną próbę odtworzenia przed
normalną pracą; Core Alarm nie posiada backendu Storage. Odtworzony latch jest aktywny,
ale ACK nie przeżywa restartu, a Domain ponownie ocenia live condition. Clear wymaga takiej
oceny i nie usuwa aktywnego condition. Application otrzymuje borrowed `AlarmControl<AlarmId>`
do ACK/clear bez dostępu do ustawiania condition; Auth i transport pozostają poza tym API.

**CURRENT F4.6 — alarm events i providers:** `AlarmEventEmitter<AlarmId>` przekazuje przez
borrowed `EventSink` semantic event tylko dla istotnego przejścia; snapshot AlarmState pozostaje
źródłem bieżącego stanu. Restore i duplicate input nie tworzą nowego occurrence. Application
może później dodać EventMetadata. `AlarmHealthProvider` i `AlarmSafetyProvider` czytają
aktualny alarm przez jawne project-owned policy callbacks; ACK, severity ani samo istnienie
alarmu nie narzucają Health/Safety mapping. Nie ma globalnego alarm registry.

**CURRENT F5.1 — Maintenance transition foundation:** czysty guard sprawdza zamiar
`RUNNING ↔ MAINTENANCE`, zwraca jawne `Allowed`, idempotentne `AlreadyInTargetState`
albo odmowę dla nielegalnego stanu/requestu. Nie zapisuje `OperationalState` ani nie
zmienia Health/Safety. `ApplicationRuntime` pozostaje jedynym writerem stanu operacyjnego;
F5.1 nie implementuje participanta ani workflow wejścia/wyjścia. MNT-101 jest
PARTIALLY ACCEPTED; `NormalDomain` i `SystemRecovery` nie są ścieżką Maintenance.

**CURRENT F5.2 — Maintenance participant foundation:** jeden borrowed
`MaintenanceParticipant` wykonuje synchroniczne `prepareEnter()` / `prepareExit()` i zwraca
`Prepared`, `Rejected` albo `Failed`. `Rejected` gwarantuje nadal prawidłowy source mode
bez nierozliczonych skutków; `Failed` jest obsługiwany fail-safe przez F5.3. Participant
nie zapisuje `OperationalState`; wykonanie przejścia pozostaje poza F5.2.

**CURRENT F5.3 — Maintenance runtime orchestration:** `ApplicationRuntime` obsługuje
synchroniczne `RUNNING ↔ MAINTENANCE` z jednym borrowed participantem. Tylko `Prepared`
zatwierdza stan docelowy; `Rejected` zachowuje stan źródłowy. `Failed` lub nielegalny
wynik przechodzi do `ERROR`, a coordinator utrzymuje `FAULT + LOCKED` także po refresh.
`ApplicationRuntime` pozostaje jedynym writerem `OperationalState`; nie ma automatycznego
restartu. Concrete service commands i autonomous processing boundary są poza F5.3.

**CURRENT F5.4 — Maintenance command policy foundation:** `CommandClass` ma klasę
`Maintenance`, a osobny typed gate rozróżnia `Enter`, `Operation` i `Exit`. Enter/Exit
przechodzą w `RUNNING` i `MAINTENANCE`, pozostawiając idempotency F5.3; `Operation` jest
dozwolone wyłącznie w `MAINTENANCE`. Gate czyta live `OperationalState` i blokuje fail-closed.
Safety policy pozostaje osobnym etapem, a konkretne operacje nie są jeszcze zaimplementowane.

**CURRENT F5.5 — normal processing boundary:** `evaluateNormalProcessing()` dopuszcza
normalne autonomous Domain processing tylko w `RUNNING`; w `MAINTENANCE`, `BOOTING`
i `ERROR` je blokuje. Application/Composition Root używa tego guardu przed normalnym
processing, bez globalnego pause flag. Intrinsic Domain/hardware safety nie może być
przez niego blokowana i działa według własnej semantyki również w Maintenance.
Restart-required należy do osobnego system mechanism z SYS-107; Maintenance nie wykonuje
restartu, a jej `OperationalState` jest ulotny i nie służy jako marker recovery po reboot.

```cpp
#include <AquaCore/System/SystemService.h>

AquaCore::SystemService system;
AquaCore::DeviceIdentity deviceIdentity(
    "luma", "Akwarium", "1.0.0", "AQMA"
);
if (system.begin(deviceIdentity)) {
    uint32_t uptime = system.uptimeMs();
    auto reason = system.restartReason();  // PowerOn, Software, Watchdog, ...
}
```

**API:**
- `begin(DeviceIdentity)` — Initialize
- `isReady()` — Is boot complete?
- `uptimeMs()` — Milliseconds since boot
- `restartReason()` — Why did we restart?
- `deviceIdentity()` — Device info (type, name, version, hardware variant)
- `status()` — Full SystemStatus struct

**Zastosowanie:**
- aquaOneLuma ✅
- aquaOneHydro ✅
- aquaOneGas ❌ (planned)

---

### Config — ✅ READY

Persistent storage with CRC32, schema versioning, dual-slot safety.

```cpp
#include <Preferences.h>
#include <AquaCore/Config/StorageRecord.h>
#include <AquaCore/Config/StorageService.h>
#include <AquaCore/Config/PreferencesStorageBackend.h>

struct MyConfig { /* ... */ };

AquaCore::Config::PreferencesStorageBackend<Preferences> backend;
uint8_t storageBuffer[
    AquaCore::Config::StorageRecord::HEADER_SIZE + sizeof(MyConfig)
] {};
AquaCore::Config::StorageWorkspace workspace {
    storageBuffer,
    sizeof(storageBuffer)
};

AquaCore::Config::StorageService storage(
    backend,
    "namespace",
    "slot_a_key",
    "slot_b_key",
    workspace
);

bool validator(const void* payload, size_t sz) {
    if (payload == nullptr || sz != sizeof(MyConfig)) {
        return false;
    }
    const auto* cfg = static_cast<const MyConfig*>(payload);
    return cfg->magic == 0xCAFEU;
}

if (storage.begin(sizeof(MyConfig), 1, validator)) {
    MyConfig cfg {};
    if (storage.load(&cfg)) {
        // ... use and modify cfg ...
    }
    if (!storage.save(&cfg)) {
        // Handle persistent write failure.
    }
}
```

**Cechy:**
- **Dual-slot atomic updates** — Safe power-loss handling
- **CRC32 verification** — Corruption detection
- **Schema versioning** — Migration support
- **Generic backend** — Not tied to Preferences API
- **Custom validator** — Per-project validation logic
- **Zero-heap workspace** — caller-owned buffer with minimum capacity `StorageRecord::HEADER_SIZE + payloadSize`

The workspace is borrowed for the full `StorageService` lifetime. Each concurrently active
service needs its own buffer; `begin()` rejects insufficient capacity without truncation or a
heap fallback. The workspace is exclusive scratch storage and must not overlap a payload passed
to `save()`.

**API:**
- `begin(payloadSize, schemaVersion, validator)` — Initialize
- `load(payload)` — Read from valid slot
- `save(payload)` — Validate and persist changed data; returns `true` for `Success` and `NoChange`
- `hasValidPayload()` — Is config loaded?
- `status()` — Detailed status (slot, generation, last op result)

`NoChange` means the selected valid record has the same schema, payload size and byte-exact
payload. It performs no backend write or readback verification and preserves the active slot and
generation. Save failures distinguish `InvalidArgument`, `ValidationFailure`, `BackendFailure`
and `VerifyFailure`. Namespace and keys must be non-null and non-empty; further length and
character constraints belong to the selected backend, including the Preferences/NVS adapter.

**CURRENT F2.6/F2.7 — Config lifecycle foundation:** `ConfigLifecycle` koordynuje jeden logiczny
rekord konfiguracji przez małe zestawy callbacków z kontekstem. Adapter projektu odpowiada za
defaults, decode, migrację krokową, walidację, apply, klasyfikację restart-required i własność
typed `ActiveConfig`; coordinator przechowuje tylko status lifecycle. Trzy rozłączne bufory
`ConfigWorkspace` (`raw`, `candidateA`, `candidateB`) są własnością Composition Root i są
borrowed przez cały czas życia coordinatora.

Startup rozróżnia empty, corrupt, future schema, migration/validation/apply/persist failure.
Defaults i zmigrowany CURRENT są zapisywane dopiero po udanym apply. Runtime update wykonuje
pełną walidację i persist desired przed live apply; restart-required zapisuje desired bez live
apply. `ConfigLifecycleStatus` ujawnia m.in. źródło, wersje stored/active, aligned, restart,
recovery i degraded, bez wartości konfiguracji. `StorageService::loadLatestRaw()` dostarcza
zweryfikowany rekord do lifecycle, a `saveCurrentRaw()` wymusza zgodny CURRENT schema/size i
zachowuje mapowanie `NoChange` jako sukces. Workspace StorageService powinien mieścić nagłówek
i największy obsługiwany zapisany payload potrzebny do migracji.

Recovery status zachowuje rozróżnienie empty, corrupt, unsupported, migration/apply/persist
failure oraz legalnej restart-required divergence. Gdy migracja startuje z nowszego old-schema
recordu, starszy byte-identical CURRENT nie daje `NoChange`: canonical CURRENT jest zapisywany
z generacją nowszą od wybranej raw bazy, aby następny startup nie powtarzał migracji.

To jest neutralna implementacja **CURRENT** kontraktu lifecycle. F2.6 wprowadziło foundation,
a F2.7 domknęło recovery integration. `StorageService` otrzymał tylko mechaniczne helpery/raw-record
operations potrzebne do współpracy z `ConfigLifecycle`; nie zawiera policy migracji, config ani recovery.
CFG-101 pozostaje szerszym **ACCEPTED — TARGET** i nie dodaje persistent LKG, two-phase active
marker, cross-record transactions, boot-loop protection, backup/restore, factory reset ani
automatycznego wykonania restartu.

**Zastosowanie:**
- aquaOneLuma ✅ (StorageService adapter)
- aquaOneHydro ✅ (HydroSenseConfigStorage adapter)
- aquaOneGas ❌ (planned)
- aquaOneDoser ✅ (StorageManager adapter: dwa StorageService + migracja legacy Preferences)

---

### Logging — ✅ READY

Unified logger with pluggable sinks, compile-time enable.

```cpp
#include <AquaCore/Logging/Logger.h>
#include <AquaCore/Logging/SerialLogSink.h>

AquaCore::SerialLogSink sink(Serial);
AquaCore::Logger logger(sink);

logger.setLevel(AquaCore::LogLevel::Info);

logger.debug("module", "message");    // Skipped if level > DEBUG
logger.info("module", "message");
logger.warning("module", "message");
logger.error("module", "message");

// Compile-time control (platformio.ini):
// build_flags = -D AQUA_CORE_LOGGING_ENABLED=1
```

**LogLevel:** DEBUG < INFO < WARNING < ERROR

**Pluggable sinks:**
- Implement `LogSink` interface
- `write(LogLevel, const char* module, const char* message)`
- SerialLogSink provided

**API:**
- `Logger(sink)` — Create with sink
- `setLevel(LogLevel)` — Filter threshold
- `debug/info/warning/error(module, msg)` — Log methods
- `compiledIn()` — Is logging enabled?

`LogWriter` is the narrow write-only capability for Domain-facing dependencies. `Logger`
implements it, so Composition Root can inject a borrowed `LogWriter&` without exposing threshold
or sink mutation. The owner must outlive the consumer. Calls are synchronous and best-effort:
`module` and `message` remain caller-owned and valid for the call, delivery is not guaranteed, and
logging has no semantic result for Domain logic. The capability uses no heap or global state.

**Zastosowanie:**
- aquaOneLuma ✅
- aquaOneHydro ❌
- aquaOneGas ✅ (Logger + SerialLogSink tworzone i używane w main.cpp)
- aquaOneDoser ✅ (Logger + SerialLogSink; część lokalnych logów nadal używa Serial)

---

### Diagnostics — ✅ READY

Health snapshot agregator.

```cpp
#include <AquaCore/Diagnostics/DiagnosticsService.h>

AquaCore::Diagnostics::DiagnosticsService diagnostics(
    system,
    rtc,
    storage,
    "Europe/Warsaw",  // time provider name
    &ntp,             // optional
    &network          // optional
);

auto snapshot = diagnostics.snapshot();
// snapshot.overallHealth — Ok/Unknown/Warning/Error
// snapshot.system.ready, .uptimeMs, .restartReason
// snapshot.time.rtcValid, .lastSyncResult, .lastSuccessfulSyncAgeMs
// snapshot.storage.backendReady, .hasValidPayload, .activeGeneration
// snapshot.network.state, .rssi, .reconnectCount
```

**Cechy:**
- **Read-only** — No side effects
- **Aggregated health** — Overall status from modules
- **Optional modules** — NTP, Network parameters

**API:**
- `snapshot()` — Get current DiagnosticsSnapshot struct

**CURRENT F6.1 — typed diagnostic provider foundation:** niezależny od powyższego legacy
service `DiagnosticProvider<Snapshot>` dostarcza caller-owned typed snapshot przez borrowed
`read(out) const`. `Success` oznacza ważny bieżący snapshot; przy `Unavailable` output nie
jest ważny. Jest to wyłącznie read-only projekcja, bez automatycznego mapowania na
Health/Safety. **CURRENT F6.2** dodaje niezależne, read-only Core projections live
`RuntimeStatus`, ukończonego startup report, `RuntimeIdentity`, gotowej `DeviceIdentity`,
`ConfigLifecycleStatus` i istniejącego `StorageStatus`. Providers czytają authoritative
owners bez duplikowania mutable authority; runtime i config failure są poprawnymi odczytami.
**CURRENT F6.3** potwierdza, że projekt domenowy używa tego samego
`DiagnosticProvider<Snapshot>` i sam definiuje snapshot oraz znaczenie jego pól.
Nie ma `IDomain` ani wspólnego Domain snapshotu; odczyt jest live i read-only.
Dodano test-only fixture, bez migracji realnej domeny.
**CURRENT F6.4** dodaje statyczny `DiagnosticRegistry<Entry>` z caller-owned entries i
deterministyczną enumeracją. Nie ma lookup ani service locatora, runtime registration ani
heterogeneous provider erasure; typed providers pozostają osobną granicą odczytu.
**CURRENT F6.5** pokazuje testową kompozycję project-owned entries oraz oddzielnych typed
Core/Domain providers. Registry tylko enumeruje metadata; odczyty odbywają się bezpośrednio
przez providers, bez service locatora, type erasure i migracji realnej domeny.

**Zastosowanie:**
- aquaOneLuma ✅
- aquaOneHydro ❌
- aquaOneGas ❌

---

### Network — ✅ READY

WiFi STA (Station) + AP (Access Point) modes.

```cpp
#include <AquaCore/Network/Esp32NetworkBackend.h>
#include <AquaCore/Network/NetworkService.h>
#include <AquaCore/Time/Esp32MonotonicClock.h>

AquaCore::Network::Esp32NetworkBackend backend;
AquaCore::Time::Esp32MonotonicClock clock;
AquaCore::Network::NetworkService network(backend, clock);

AquaCore::Network::NetworkConfig config;
config.staEnabled = true;
snprintf(config.ssid, sizeof(config.ssid), "%s", "MyWiFi");
snprintf(config.password, sizeof(config.password), "%s", "pass123");
snprintf(config.hostname, sizeof(config.hostname), "%s", "aqua-luma");
config.autoReconnect = true;
config.reconnectIntervalMs = 10000;

if (network.begin(config)) {
    // In loop():
    network.update();
    
    if (network.isConnected()) {
        const AquaCore::Network::IpAddress ip = network.ipAddress();
        Serial.printf("IP: %u.%u.%u.%u\n",
                      ip.octets[0], ip.octets[1],
                      ip.octets[2], ip.octets[3]);
    }
}
```

**State machine:**
- `Disabled`, `Idle`, `Connecting`, `Connected`, `Disconnected`, `Error`
- `Disabled` is a legal optional configuration; `Idle` includes AP-only with no STA attempt.
- `Connected` reflects the backend's STA-connected report (`WL_CONNECTED` on ESP32), not Internet, DNS, NTP, MQTT, or Web availability.
- AP state is separate from STA state; AP startup is configuration-driven, with no automatic STA-loss fallback.
- Tracks connection uptime, reconnect count, RSSI

F7.4 CURRENT formalizes this platform-neutral state/API and the ESP32 backend boundary.
Network remains optional for autonomous Domain operation; Network failure does not itself
set Runtime Health/Safety or stop Domain processing. F7.5 CURRENT uses the borrowed
`MonotonicClock` for 64-bit retry, fast retry, timeout, and connection uptime while retaining
the existing intervals. The optional `NetworkStartup` participant runs in `NETWORK_INIT`:
disabled STA/AP returns Disabled without degradation, while an initialization failure can
leave startup RUNNING + DEGRADED. `NetworkHealthProvider` reads live state, so recovery can
restore Health OK; it never contributes a Safety lock. Network Connected still does not
guarantee Internet reachability.
`NetworkService` borrows its backend, and its status getters do not expose passwords.

**AP (Access Point) mode:**
```cpp
config.apEnabled = true;
snprintf(config.apSsid, sizeof(config.apSsid), "%s", "AquaOne-Setup");
snprintf(config.apPassword, sizeof(config.apPassword), "%s", "setup123");
// network.startAccessPoint() called internally if needed
```

**API:**
- `begin(NetworkConfig)` — Initialize
- `update()` — Process state machine using the borrowed monotonic clock
- `update(uint32_t nowMs)` — Compatibility adapter for existing `millis()` callers; required with the legacy constructor
- `isConnected()` — Is STA mode connected?
- `state()` — Current NetworkState enum
- `ipAddress()` — Current IP
- `rssi()` — Signal strength (dBm)
- `connectionUptimeMs()` — 64-bit duration since connection
- `connectionUptimeMs(uint32_t nowMs)` — Compatibility view
- `reconnectCount()` — How many reconnects

**Zastosowanie:**
- aquaOneLuma ✅
- aquaOneHydro ✅
- aquaOneGas ❌ (planned)
- aquaOneDoser ✅ (lokalny WiFiManager deleguje do NetworkService/Esp32NetworkBackend)

---

### Web — ✅ READY

HTTP server with routing, provider pattern.

**CURRENT:** `Esp32WebBackend` uses the legacy Arduino `WebServer` for HTTP.
**TARGET (WEB-103):** native ESP-IDF `esp_http_server` on Arduino-ESP32 is
selected for HTTP + WebSocket. Production migration is Phase 9; it is not yet
implemented here.

**CURRENT F9.2 foundation:** `EspIdfWebTransport` is an application-owned
`esp_http_server` transport alongside the legacy backend. It has fixed, bounded
GET/POST registration frozen before start, one HTTPD handle/port, a single 404
handler, and begin/stop/restart lifecycle. Callbacks run in the HTTPD server
task. F9.2 accepts bodyless routes only; snapshot publication, serialized
actions, upload/Auth and Realtime semantics are later Phase 9 work. Products
still use `Esp32WebBackend` and the Arduino `WebServer`.
Legacy body/upload behavior, including `maxBodyLength == 0`, requires an
explicit compatibility decision during F9.7/WEB-102 migration.

**CURRENT F9.3 foundation:** `PublishedSnapshot<T>` lets one Application writer
publish a bounded, self-contained typed Web projection. Readers obtain their own
copy under a borrowed synchronizer, then serialize after unlock; before the
first publish and after invalidation, the resource is unavailable. ESP32 uses
an owner-held static FreeRTOS mutex. This projection is not Domain authority;
watermark/resync and Realtime semantics are still later Phase 9 work. No product
composition uses this foundation yet.

**CURRENT F9.4 foundation:** `WebActionBridge<Command, Capacity>` accepts an
owned, bounded typed command into a fixed FIFO and invokes the existing
`CommandPipeline` only from the serialized Application context. Each accepted
request has bridge-owned response storage and a local slot/generation token.
Wait is bounded; timeout does not cancel an accepted command, and a late result
is safely discarded after its waiter abandons it. `CommandExecutionResult`,
including `OperationStarted`, is preserved. HTTP mapping, Auth and idempotency
remain later WEB-102/CMD-101/CMD-102 work. Each accepted token must be consumed
by one bounded wait or explicit abandon; retry after timeout is unsafe without
future idempotency. Generation is runtime-local `uint64_t`, not a durable ID.
WS publication, operation IDs and final HTTP body/response schema remain
outside this foundation.

**CURRENT F9.5 foundation:** one optional WS endpoint belongs to the same
`EspIdfWebTransport` HTTPD handle. Application assigns its own RuntimeIdentity
and Realtime stream sequence, copies a bounded prepared frame into an owned
work slot, then queues work for the HTTPD task. Acceptance means only that
boundary accepted the copy, never client presence, socket success or delivery
acknowledgement. The fixed four-slot pool and 256-byte payload are F9.5
implementation capacities, not protocol limits. Publication and stop share a
lifecycle mutex; stop releases it before blocking in `httpd_stop()` and reclaims
slots only after server callbacks end. Incoming bounded frames are consumed and
rejected without invoking Domain commands. EventSequence, wire envelope, Auth,
snapshot watermark/resync and final backpressure policy remain deferred.

```cpp
#include <AquaCore/Web/Esp32WebBackend.h>
#include <AquaCore/Web/WebService.h>

AquaCore::Web::Esp32WebBackend backend;
AquaCore::Web::WebService web(
    backend,
    systemService,        // optional
    &diagnosticsService   // optional
);

AquaCore::Web::WebConfig webConfig;
webConfig.enabled = true;
webConfig.port = 80;
webConfig.navigationMask =
    AquaCore::Web::navigationSectionMask(
        AquaCore::Web::NavigationSection::Dashboard
    );

class StatusApi final : public AquaCore::Web::WebApiProvider {
public:
    const char* route() const override { return "/api/status"; }
    AquaCore::Web::HttpMethod method() const override {
        return AquaCore::Web::HttpMethod::Get;
    }
    void handle(
        const AquaCore::Web::WebRequest&,
        AquaCore::Web::WebResponseWriter& response
    ) override {
        if (response.beginResponse(200, AquaCore::Web::ContentType::Json)) {
            response.writeText("{\"status\":\"ok\"}");
            response.endResponse();
        }
    }
};

StatusApi statusApi;
if (web.addApi(statusApi) && web.begin(webConfig)) {
    // In loop():
    web.update();
}
```

**Built-in routes:**
- `GET /` — Home page
- `GET /assets/aqua.css` — CSS
- `GET /api/system` — System info (JSON)
- `GET /api/diagnostics` — Diagnostics (JSON)
- `404` — Custom not found handler

**Provider pattern:**
```cpp
class StatusPage final : public AquaCore::Web::WebPageProvider {
public:
    const char* route() const override { return "/status"; }
    const char* title() const override { return "Status"; }
    void render(AquaCore::Web::WebResponseWriter& response) const override {
        response.writeText("<h1>Status</h1>");
    }
};
```

Providery i własne trasy należy zarejestrować przed `web.begin()`.

**API:**
- `begin(WebConfig)` — Initialize server
- `update()` — Handle requests (call in loop)
- `stop()` — Shutdown
- `isRunning()` — Server active?
- `addRoute(path, method, handler, context)` — Register endpoint
- `addPage(WebPageProvider&)` — Register provider
- `addApi(WebApiProvider&)` — Register API provider

**Zastosowanie:**
- aquaOneLuma ✅
- aquaOneHydro ✅
- aquaOneGas ❌ (planned)
- aquaOneDoser ✅ (jeden Esp32WebBackend/WebService + lokalny WebManager dla domain policy)

---

### Time — ✅ READY

F7.1 CURRENT foundation: `MonotonicClock` is a small, platform-neutral borrowed
capability returning nondecreasing, runtime-local `uint64_t` milliseconds for
timeouts, durations and retry. `Esp32MonotonicClock` uses the ESP32 monotonic
timer. It is independent of wall clock, RTC, NTP and Network. Existing Time
and Network callers still use their `millis()` / `nowMs` APIs until later
integration; F7.1 does not change their behavior. `uint64_t` overflow is
outside the practical range of a device runtime.

F7.2 CURRENT foundation adds a borrowed `WallClock` UTC capability. Its
`Success` / `Unavailable` read result determines whether the 64-bit Unix-second
value is usable; zero is a valid value on `Success`. Wall time may jump forward
or backward, so `MonotonicClock` remains the capability for elapsed timing.
F7.2 itself left RTC/NTP source policy open; F7.6 defines the v1 RTC-backed
WallClock and optional NTP synchronization contract below.

F7.3 CURRENT adds `RtcWallClock`, a borrowed read-only projection of valid RTC
UTC into `WallClock`. A valid RTC provides wall time offline; an uninitialized,
unreadable, OSF/lost-power or invalid RTC yields `Unavailable`. Network and NTP
are not required for the read. NTP remains an optional synchronizer, while
local time and Europe/Warsaw DST remain a separate projection.

Time module zawiera cztery komponenty: RtcService (DS3231), NtpService (synchronization),
EuropeWarsawTimeService (UTC to local time conversion with DST) oraz ResilientTimeService
(cache czasu, progi awarii i recovery RTC).

#### RtcService

DS3231 RTC chip over I2C.

```cpp
#include <AquaCore/Time/RtcService.h>

AquaCore::Time::RtcBus& rtcBus = ...;  // Adapter dla Wire
AquaCore::Time::RtcConfig config;
config.sdaPin = GPIO_NUM_20;
config.sclPin = GPIO_NUM_21;
config.i2cAddress = 0x68;

AquaCore::Time::RtcService rtc(rtcBus, config);

if (rtc.begin()) {
    AquaCore::Time::LocalTime now = rtc.read();
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year, now.month, now.day,
                  now.hour, now.minute, now.second);
    
    // Set time from external source
    rtc.setUtc(2026, 9, 11, 12, 30, 45);
}
```

**API:**
- `begin()` — Initialize I2C, test DS3231
- `read()` — Read current time (LocalTime struct)
- `isValid()` — Was last read successful?
- `isInitialized()` — Is DS3231 responsive?
- `setUtc(year, month, day, hour, minute, second)` — Set time

#### NtpService

NTP synchronization via esp_sntp (non-blocking, async).

F7.6 CURRENT composes `NtpService(rtc, backend, monotonicClock)` with
`NetworkNtpSync(ntp, network)`. After `ntp.begin()`, call `network.update()` and then
`ntpSync.update()` in runtime processing, including Maintenance. Only live Network
Connected starts an attempt; it does not guarantee NTP success. Timeout and periodic
retry use the 64-bit monotonic clock. A successful UTC fetch updates and verifies RTC
before `lastSyncSucceeded()` becomes true; `lastFetchSucceeded()` remains separate.
`RtcWallClock` reads that RTC as the only consumer-facing UTC source. A valid RTC stays
usable offline or after an NTP fetch failure that did not write RTC; an invalid RTC can
recover after a successful NTP write. Failed or partial RTC writes have no rollback
guarantee. NTP does not block startup or contribute Health/Safety. Local time and DST
remain a separate projection. Legacy `uint32_t nowMs` overloads remain for Luma and Doser;
their rollover adapter requires an observation at least once per full 32-bit `millis()`
cycle. The borrowed 64-bit clock has no such limit.

```cpp
#include <AquaCore/Time/NtpService.h>
#include <AquaCore/Time/Esp32MonotonicClock.h>
#include <AquaCore/Time/NetworkNtpSync.h>

AquaCore::Time::Esp32MonotonicClock monotonicClock;
AquaCore::Time::NtpService ntp(rtcService, ntpBackend, monotonicClock);
AquaCore::Time::NetworkNtpSync ntpSync(ntp, network);

ntp.begin();  // Initialize only; startup does not wait for NTP.

// In loop():
network.update();
ntpSync.update();

if (ntp.lastSyncSucceeded()) {
    Serial.println("NTP: Synchronized!");
    // DS3231 was updated with UTC time
}
```

**State machine:**
- `requestSync(wifiAvailable, nowMs)` — Start sync
- `requestPeriodicSync(wifiAvailable, nowMs)` — Check if due (24h default)
- `update(wifiAvailable, nowMs)` — Poll for result
- Result: Pending → Success/Failure
- On Success: RTC (DS3231) is updated automatically

**Non-blocking:**
- `poll()` returns immediately (Pending/Success/Failure)
- No blocking waits
- Timeout handling (10s default)

**API:**
- `begin(nowMs)` / `begin(NtpConfig, nowMs)` — Initialize; does not request a sync
- `requestSync(wifiAvailable, nowMs)` — Request immediate sync
- `requestPeriodicSync(wifiAvailable, nowMs)` — Check if periodic sync is due
- `update(wifiAvailable, nowMs)` — Process poll result
- `isSyncInProgress()` — Awaiting result?
- `lastSyncSucceeded()` — Did last sync work?
- `isPeriodicSyncDue(nowMs)` — Is 24h interval reached?
- `lastSuccessfulSyncAgeMs(nowMs, ageMs)` — How old is last sync?

#### EuropeWarsawTimeService

UTC → Europe/Warsaw timezone conversion with DST rules.

```cpp
#include <AquaCore/Time/EuropeWarsawTimeService.h>

AquaCore::Time::EuropeWarsawTimeService timeService(rtcService);

if (timeService.begin()) {
    AquaCore::Time::LocalTime now = timeService.now();
    // now = Europe/Warsaw time (CET/CEST with DST applied)
}
```

**Cechy:**
- UTC read from RTC
- DST rules applied (last Sunday of March/October)
- Caches current time

**API:**
- `begin()` — Initialize
- `now()` — Get current Europe/Warsaw time
- `isValid()` — Is cached time valid?
- `current()` — Get cached (last read) time

#### ResilientTimeService

`ResilientTimeService` utrzymuje monotoniczny cache czasu i monitoruje kondycję RTC z
konfigurowalnymi progami failure/recovery. Doser używa go przez lokalny `TimeManager`.

```cpp
#include <AquaCore/Time/ResilientTimeService.h>

AquaCore::Time::ResilientTimeConfig resilientConfig {};
resilientConfig.failureThreshold = 3;
resilientConfig.recoveryThreshold = 3;

AquaCore::Time::ResilientTimeService resilientTime(rtc, resilientConfig);
resilientTime.begin(millis());

// In loop():
resilientTime.poll(millis());
const AquaCore::Time::LocalTime now = resilientTime.now(millis());
```

**API:**
- `begin(nowMs)` / `poll(nowMs)` — Initialize and probe RTC
- `now(nowMs)` — Current cached time advanced monotonically
- `isValid()` / `isRtcHealthy()` — Cache and RTC health
- `syncUtc(utc, nowMs)` — Update cache after an external synchronization

---

## 🔧 Integration Guide

### Minimalna integracja

```cpp
// main.cpp
#include <AquaCore/System/SystemService.h>
#include <AquaCore/Network/Esp32NetworkBackend.h>
#include <AquaCore/Network/NetworkService.h>
#include <AquaCore/Web/Esp32WebBackend.h>
#include <AquaCore/Web/WebService.h>

AquaCore::SystemService system;
AquaCore::Network::Esp32NetworkBackend netBackend;
AquaCore::Network::NetworkService network(netBackend);
AquaCore::Web::Esp32WebBackend webBackend;
AquaCore::Web::WebService web(webBackend);

AquaCore::DeviceIdentity identity(
    "device", "name", "1.0.0", "hardware"
);

void setup() {
    system.begin(identity);
    
    AquaCore::Network::NetworkConfig netConfig;
    // ... fill config ...
    network.begin(netConfig);
    
    AquaCore::Web::WebConfig webConfig;
    webConfig.enabled = true;
    // Register product routes/providers before begin().
    web.begin(webConfig);
}

void loop() {
    network.update(millis());
    web.update();
}
```

### Pełna integracja (Luma pattern)

Przejrzyj [aquaOneLuma/src/main.cpp](../aquaOneLuma/src/main.cpp) — Przykład wszystkich modułów Core.

---

## 📦 Setup w platformio.ini

```ini
[env:esp32-s3]
platform = espressif32
framework = arduino
lib_deps =
    symlink://../aquaOneCore
```

Core jest dostępny w tym samym workspace, na poziomie katalogów projektów.

**Zależności Core:**
- Arduino (Serial, Wire, WiFi, Preferences)
- ESP-IDF (esp_sntp, esp_system)
- Standard C++ (time.h, cstring)

Brak zewnętrznych bibliotek — Core pozostaje lekki.

---

## 🚀 Przyszłe rozszerzenia (PLANNED, no versions assigned)

- **MQTT Module** — Message broker integration
- **Core OTA** — Shared firmware update orchestration, signing and rollback
- **Home Assistant Integration** — Discovery and entity mapping
- **Alarm/Safety** — alarm state foundation i Action Lock foundation są CURRENT; pełny framework pozostaje planowany

Doser W1.5 ma lokalne OTA jako **CURRENT**. Nie jest to implementacja wspólnego Core OTA,
które pozostaje **TARGET/FUTURE**.

---

## ⚠️ Ograniczenia i wiadome problemy

**Nie zaimplementowane:**
- MQTT module
- shared Core OTA
- Home Assistant integration
- pełne Alarm/Safety Core modules
- Generic TimezoneProvider (Europe/Warsaw is hardcoded dla teraz)

**Znane ograniczenia:**
- Network module: Nie obsługuje WPA3 (ESP32 limitation)
- Web module: HTTP only (no HTTPS)
- Time: Nie obsługuje leap seconds (RTC limitation)

---

## 📞 Support

Pytania/problemy:
- Przejrzyj [aquaOne/docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)
- Przejrzyj [aquaOne/docs/PROJECT_MATRIX.md](../docs/PROJECT_MATRIX.md)
- Poszukaj wzorca w [aquaOneLuma](../aquaOneLuma) lub [aquaOneHydro](../aquaOneHydro)

---

## 📄 License

[TBD]
