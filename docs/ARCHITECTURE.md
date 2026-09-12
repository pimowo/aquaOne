# Architektura ekosystemu aquaOne

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

Każde urządzenie (Luma, Doser, Hydro, Gas, ...) ma strukturę:

```
aquaOneXxx/
├── platformio.ini
│   └── lib_extra_dirs = ../aquaOneCore  ← lokalna zależność od Core
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

- **W1 (zakończony):** neutralny transport AquaCore Web i pojedynczy backend ESP32;
- **W1.5 (bieżący):** most Dosera dla autoryzowanego restartu i OTA na wspólnym serwerze;
- **W2 (później):** dalsza migracja stron/API produktu, dopiero po testach sprzętowych W1.5.

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
    Initializing,
    Ready,
    Connected,
    Disconnected
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

Każdy projekt ma swoją wersję:
```ini
; aquaOneLuma/platformio.ini
version = X.Y.Z
```

Core ma wersję:
```
aquaOneCore/library.json
version = 0.6.2
```

Projekty mogą przypiąć konkretny commit Core w celu zapewnienia reproducible builds:
```bash
git -C ../aquaOneCore checkout <commit-sha>
```

Core rozwija się niezależnie; projekty integrują wybrane moduły Core.

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
