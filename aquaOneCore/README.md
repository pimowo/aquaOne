# aquaOneCore v0.6.2

Wspólna biblioteka technicznych usług dla ekosystemu aquaOne.

Core dostarcza abstrakcje dla funkcji wspólnych do wszystkich urządzeń — boot, persistent storage, czas (RTC + NTP), sieć (WiFi), web server, logging, diagnostyka. Nie zawiera logiki domenowej (oświetlenie, pompy, czujniki, algorytmy).

## 🟢 Status: STABLE & IN USE

- 7 modułów zaimplementowanych i weryfikowanych
- Używana (aquaOneLuma), integrowana (aquaOneHydro)
- API wdrażane; brak zmian breaking w bliskiej przyszłości

## 📦 Moduły (v0.6.2)

### System — ✅ READY

Boot status, device identity, restart reasons.

```cpp
#include <AquaCore/System/SystemService.h>

AquaCore::SystemService system;
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
#include <AquaCore/Config/StorageService.h>
#include <AquaCore/Config/PreferencesStorageBackend.h>

Preferences prefs;
AquaCore::Config::PreferencesStorageBackend<Preferences> backend;

AquaCore::Config::StorageService storage(
    backend,
    "namespace",
    "slot_a_key",
    "slot_b_key"
);

struct MyConfig { /* ... */ };

bool validator(const void* payload, size_t sz) {
    auto cfg = (MyConfig*)payload;
    return cfg->magic == 0xCAFE;
}

if (storage.begin(sizeof(MyConfig), 1, validator)) {
    MyConfig cfg;
    storage.load(&cfg);
    // ... modify ...
    storage.save(&cfg);
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
- aquaOneDoser ❌ (uses raw Preferences)

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
- aquaOneGas ⚠️ (imports, nie faktycznie używa)

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
// snapshot.overallHealth — Good/Degraded/Failed
// snapshot.system.state, .uptime, .restartReason
// snapshot.time.rtcValid, .ntpValid, .lastSyncAge
// snapshot.storage.backendReady, .hasValidPayload
// snapshot.network.state, .signalStrength
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
config.ssid = "MyWiFi";
config.password = "pass123";
config.hostname = "aqua-luma";
config.autoReconnect = true;
config.reconnectIntervalMs = 10000;

if (network.begin(config)) {
    // In loop():
    network.update(millis());
    
    if (network.isConnected()) {
        Serial.print("IP: ");
        Serial.println(network.ipAddress());
    }
}
```

**State machine:**
- Disabled → Initializing → Ready → Connected/Disconnected
- Auto-retry with backoff
- Tracks connection uptime, reconnect count, RSSI

**AP (Access Point) mode:**
```cpp
config.apEnabled = true;
config.apSsid = "AquaOne-Setup";
config.apPassword = "setup123";
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
- aquaOneDoser ❌ (uses WiFiManager)

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
webConfig.port = 80;
webConfig.navigationSections = /* flags */;

if (web.begin(webConfig)) {
    // Register custom pages/APIs
    class MyPages : public WebPageProvider {
        void registerRoutes(WebService& web) override {
            web.addRoute("/api/status", GET, [](const WebRequest& req, WebResponseWriter& resp) {
                resp.json("{\"status\":\"ok\"}");
            });
        }
    };
    
    MyPages pages;
    web.addPage(pages);
    
    // In loop():
    web.update();
}
```

**Built-in routes:**
- `GET /` — Home page
- `GET /style.css` — CSS
- `GET /api/system` — System info (JSON)
- `GET /api/diagnostics` — Diagnostics (JSON)
- `404` — Custom not found handler

**Provider pattern:**
```cpp
class MyApiProvider : public WebApiProvider {
    void registerRoutes(WebService& web) override {
        // Register GET, POST, PUT, DELETE handlers
    }
};
```

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
- aquaOneDoser ❌ (uses WebManager)

---

### Time — ✅ READY

Time module zawiera trzy komponenty: RtcService (DS3231), NtpService (synchronization), oraz EuropeWarsawTimeService (UTC to local time conversion with DST).

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

// Initialize
if (ntp.begin()) {  // Uses default config
    // NTP is now running in background
}

// In loop():
network.update(millis());  // Update WiFi state

if (networkService.isConnected()) {
    ntp.update(true, millis());  // Trigger periodic sync
} else {
    ntp.update(false, millis());  // Handle WiFi loss
}

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
- `begin(NtpConfig)` — Initialize with config
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

void setup() {
    AquaCore::DeviceIdentity identity("device", "name", "1.0.0", "hardware");
    system.begin(identity);
    
    AquaCore::Network::NetworkConfig netConfig;
    // ... fill config ...
    network.begin(netConfig);
    
    AquaCore::Web::WebConfig webConfig;
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
lib_extra_dirs = ../aquaOneCore
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
- **OTA Updates** — Firmware update orchestration
- **Home Assistant Integration** — Discovery and entity mapping
- **RTC Health Monitoring** — Optional recovery & cache (backlog)

---

## ⚠️ Ograniczenia i wiadome problemy

**Nie zaimplementowane:**
- MQTT module
- OTA updates
- Home Assistant integration
- Generic TimezoneProvider (Europe/Warsaw is hardcoded dla teraz)
- RTC health monitoring (candidate, optional module)

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
