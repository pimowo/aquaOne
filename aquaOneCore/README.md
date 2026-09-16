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

## 📦 Moduły (v0.6.2)

### System — ✅ READY

Boot status, device identity, restart reasons.

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
#include <AquaCore/Config/StorageService.h>
#include <AquaCore/Config/PreferencesStorageBackend.h>

AquaCore::Config::PreferencesStorageBackend<Preferences> backend;

AquaCore::Config::StorageService storage(
    backend,
    "namespace",
    "slot_a_key",
    "slot_b_key"
);

struct MyConfig { /* ... */ };

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

**API:**
- `begin(payloadSize, schemaVersion, validator)` — Initialize
- `load(payload)` — Read from valid slot
- `save(payload)` — Atomic write to next slot
- `hasValidPayload()` — Is config loaded?
- `status()` — Detailed status (slot, generation, last op result)

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
