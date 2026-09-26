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
hardwareVariant 32 bajty. Istniejące typy i foundation pozostają CURRENT i nie implementują pełnego TARGET RuntimeIdentity ani docelowego DeviceIdentity contract.
Format device_id, MAC/MAC6, finalne pola, capacities i walidacja są opisane przez zaakceptowany kierunek IDN-101.
Statusy kontraktów docelowych: SYS-101 — ACCEPTED — TARGET; IDN-101 — ACCEPTED — TARGET. RuntimeIdentity (SYS-101) i jego generator nie są zaimplementowane; obecny kod nie implementuje pełnego TARGET contract.
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
aktywnych providerów; nie utrzymuje reason registry, nie zapisuje SafetyState i nie jest jeszcze
zintegrowany z CommandPipeline (to pozostaje F3.5). Action ID i provider należą do Domain/projektu;
Composition Root posiada providerów oraz listę. Nie jest to pełny Safety framework ani Alarm/
Maintenance implementation.

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

AquaCore::Network::Esp32NetworkBackend backend;
AquaCore::Network::NetworkService network(backend);

AquaCore::Network::NetworkConfig config;
config.staEnabled = true;
snprintf(config.ssid, sizeof(config.ssid), "%s", "MyWiFi");
snprintf(config.password, sizeof(config.password), "%s", "pass123");
snprintf(config.hostname, sizeof(config.hostname), "%s", "aqua-luma");
config.autoReconnect = true;
config.reconnectIntervalMs = 10000;

if (network.begin(config)) {
    // In loop():
    network.update(millis());
    
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
- Auto-retry with backoff
- Tracks connection uptime, reconnect count, RSSI

**AP (Access Point) mode:**
```cpp
config.apEnabled = true;
snprintf(config.apSsid, sizeof(config.apSsid), "%s", "AquaOne-Setup");
snprintf(config.apPassword, sizeof(config.apPassword), "%s", "setup123");
// network.startAccessPoint() called internally if needed
```

**API:**
- `begin(NetworkConfig)` — Initialize
- `update(nowMs)` — Process state machine (call in loop)
- `isConnected()` — Is STA mode connected?
- `state()` — Current NetworkState enum
- `ipAddress()` — Current IP
- `rssi()` — Signal strength (dBm)
- `connectionUptimeMs(nowMs)` — How long connected
- `reconnectCount()` — How many reconnects

**Zastosowanie:**
- aquaOneLuma ✅
- aquaOneHydro ✅
- aquaOneGas ❌ (planned)
- aquaOneDoser ✅ (lokalny WiFiManager deleguje do NetworkService/Esp32NetworkBackend)

---

### Web — ✅ READY

HTTP server with routing, provider pattern.

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

```cpp
#include <AquaCore/Time/NtpService.h>

AquaCore::Time::NtpService ntp(rtcService, ntpBackend);

const uint32_t startedAtMs = millis();
if (ntp.begin(startedAtMs)) {  // Initialize state; does not start a sync.
    ntp.requestSync(network.isConnected(), startedAtMs);
}

// In loop():
network.update(millis());  // Update WiFi state
const uint32_t nowMs = millis();
const bool wifiAvailable = network.isConnected();
ntp.requestPeriodicSync(wifiAvailable, nowMs);
ntp.update(wifiAvailable, nowMs);

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
- **Alarm/Safety** — Shared contracts are documented, but no Core module exists yet

Doser W1.5 ma lokalne OTA jako **CURRENT**. Nie jest to implementacja wspólnego Core OTA,
które pozostaje **TARGET/FUTURE**.

---

## ⚠️ Ograniczenia i wiadome problemy

**Nie zaimplementowane:**
- MQTT module
- shared Core OTA
- Home Assistant integration
- Alarm/Safety Core modules
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
