# aquaOne

### Autonomiczny ekosystem sterowników ESP32 dla akwarium

**aquaOne** to rozwijany modułowy ekosystem urządzeń do automatyzacji akwarium.
Oświetlenie, dozowanie nawozów, dolewka wody, kontrola CO₂, klimat i karmienie są realizowane przez **niezależne sterowniki**, które mogą współpracować ze sobą, ale nie są od siebie wymagane do podstawowego działania.

> **Podstawowa zasada aquaOne:** akwarium ma działać także wtedy, gdy nie działa Wi-Fi, MQTT, Home Assistant albo inne urządzenie systemu.

Projekt rozwijany jest w **C++ / PlatformIO** głównie dla układów **ESP32 / ESP32-S3**.

---

## Idea projektu

aquaOne nie jest jednym wielkim sterownikiem.

To zestaw wyspecjalizowanych urządzeń:

```text
                           ┌─────────────────────┐
                           │     aquaOneCore     │
                           │ wspólna warstwa     │
                           │    techniczna       │
                           └──────────┬──────────┘
                                      │
              ┌───────────────┬───────┼───────┬───────────────┐
              │               │       │       │               │
              ▼               ▼       ▼       ▼               ▼
        aquaOneLuma     aquaOneDoser  Hydro   Gas            Clima
        oświetlenie       nawozy      woda    CO₂            klimat

                                      │
                                      ▼
                                aquaOneFauna
                                  karmienie
```

Każdy sterownik:

* posiada własną logikę domenową,
* działa autonomicznie,
* przechowuje potrzebną konfigurację lokalnie,
* może posiadać własny panel WWW,
* może korzystać z MQTT / Home Assistant,
* korzysta tylko z potrzebnych elementów `aquaOneCore`,
* nie importuje kodu innych urządzeń.

---

# Projekty

| Projekt          | Status                          | Przeznaczenie                                      |
| ---------------- | ------------------------------- | -------------------------------------------------- |
| **aquaOneLuma**  | 🟢 stabilny / referencyjny      | Sterowanie wielokanałowym oświetleniem LED         |
| **aquaOneDoser** | 🟡 funkcjonalny / migracja Core | Automatyczne dozowanie nawozów                     |
| **aquaOneHydro** | 🟢 funkcjonalny                 | Automatyczna dolewka i kontrola poziomu wody       |
| **aquaOneGas**   | 🟡 rozwój                       | Monitoring butli CO₂                               |
| **aquaOneClima** | 🔴 szkielet                     | Temperatura, ogrzewanie, chłodzenie i obieg        |
| **aquaOneFauna** | 🔴 szkielet                     | Automatyczne karmienie i funkcje związane z obsadą |
| **aquaOneCore**  | 🟢 stabilny                     | Wspólna infrastruktura techniczna                  |

Szczegółowy i aktualny stan implementacji:

**[docs/PROJECT_MATRIX.md](docs/PROJECT_MATRIX.md)**

---

# aquaOneCore

`aquaOneCore` jest wspólną biblioteką techniczną dla urządzeń aquaOne.

Core **nie zna akwarium**.

Nie wie czym jest:

* lampa,
* pompa,
* nawożenie,
* poziom wody,
* butla CO₂,
* grzałka,
* karmnik.

Dostarcza wyłącznie mechanizmy techniczne, które w przeciwnym razie trzeba byłoby implementować osobno w każdym urządzeniu.

### Aktualne moduły Core

| Moduł           | Status | Funkcja                                                |
| --------------- | ------ | ------------------------------------------------------ |
| **System**      | ✅      | boot, uptime, identyfikacja urządzenia, restart reason |
| **Config**      | ✅      | trwała konfiguracja, CRC32, versioning, dual-slot      |
| **Logging**     | ✅      | wspólny logger i wymienne output sinks                 |
| **Diagnostics** | ✅      | agregacja stanu i diagnostyki                          |
| **Network**     | ✅      | Wi-Fi, STA/AP, reconnect i state machine               |
| **Web**         | ✅      | serwer HTTP, routing, API i provider pattern           |
| **Time**        | ✅      | RTC, NTP, recovery i strefa Europe/Warsaw              |
| **MQTT**        | LATER  | brak implementacji Core; najpierw wymagany T0 ESP-MQTT |
| **OTA**         | TARGET | wspólny moduł nie istnieje; Doser ma lokalne OTA       |

Aktualna wersja:

```text
aquaOneCore 0.6.2
```

Dokumentacja biblioteki:

**[aquaOneCore/README.md](aquaOneCore/README.md)**

---

# Autonomia przede wszystkim

Sieć jest dodatkiem, a nie fundamentem działania urządzenia.

```text
                        ┌──────────────┐
                        │   Internet   │
                        └──────┬───────┘
                               │ opcjonalnie
                        ┌──────▼───────┐
                        │ Wi-Fi / MQTT │
                        │      / HA    │
                        └──────┬───────┘
                               │
                               ▼
┌──────────────────────────────────────────────────┐
│               urządzenie aquaOne                │
│                                                  │
│  hardware → logika domenowa → lokalna kontrola  │
│                                                  │
│               DZIAŁA BEZ SIECI                  │
└──────────────────────────────────────────────────┘
```

Awaria:

```text
Wi-Fi             → urządzenie działa dalej
MQTT              → urządzenie działa dalej
Home Assistant    → urządzenie działa dalej
Internet          → urządzenie działa dalej
inny moduł        → urządzenie działa dalej
```

Panel WWW, MQTT czy Home Assistant rozszerzają funkcjonalność, ale nie powinny być potrzebne do wykonywania podstawowego zadania urządzenia.

---

# Architektura

Nowe projekty powinny rozdzielać logikę urządzenia od sprzętu i infrastruktury.

Przykładowa struktura:

```text
aquaOneXxx/
├── platformio.ini
│
├── include/
│   ├── BuildConfig.h
│   ├── NetworkSecrets.h
│   ├── XxxConfig.h
│   └── app/
│       └── XxxApp.h
│
├── src/
│   ├── main.cpp
│   │
│   ├── app/
│   │   └── XxxApp.cpp
│   │
│   ├── domain/
│   │   └── logika urządzenia
│   │
│   ├── drivers/
│   │   └── obsługa hardware
│   │
│   ├── services/
│   │   └── algorytmy i obliczenia
│   │
│   └── interfaces/
│       ├── Web
│       └── MQTT
│
└── test/
```

Kierunek zależności:

```text
                 Device App
                     │
        ┌────────────┼────────────┐
        │            │            │
        ▼            ▼            ▼
     Domain       Drivers      Interfaces
        │
        │
        └───────────────┐
                        ▼
                   aquaOneCore
```

Najważniejsza zasada:

```text
urządzenie → Core     ✅

Core → urządzenie     ❌

Luma → Doser          ❌
Hydro → Gas           ❌
Doser → Hydro         ❌
```

Komunikacja między urządzeniami, jeśli jest potrzebna, odbywa się przez zdefiniowany interfejs komunikacyjny, a nie przez współdzielenie kodu domenowego.

---

## Documentation / Source of Truth

README jest mapą repozytorium i punktem wejścia. Nie zastępuje dokumentów źródłowych ani nie
jest konkurencyjnym źródłem wymagań.

| Dokument | Rola |
| --- | --- |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | Nadrzędna architektura i granice odpowiedzialności |
| [PROJECT_MATRIX.md](docs/PROJECT_MATRIX.md) | Snapshot aktualnej implementacji na wskazanym commit/date |
| [ROADMAP.md](docs/ROADMAP.md) | Przyszłe prace i kolejność etapów |
| `docs/*_STANDARD.md` | Kontrakty techniczne; nie są same w sobie dowodem implementacji |
| [IDEAS.md](docs/IDEAS.md) | Niezatwierdzone pomysły; nie jest wymaganiem ani roadmapą |

Aktywne standardy techniczne:

- [ALARM_STANDARD.md](docs/ALARM_STANDARD.md)
- [CONFIG_STORAGE_STANDARD.md](docs/CONFIG_STORAGE_STANDARD.md)
- [DIAGNOSTICS_STANDARD.md](docs/DIAGNOSTICS_STANDARD.md)
- [FACTORY_RESET_ONBOARDING_STANDARD.md](docs/FACTORY_RESET_ONBOARDING_STANDARD.md)
- [MQTT_STANDARD.md](docs/MQTT_STANDARD.md)
- [NAMING_VERSIONING_STANDARD.md](docs/NAMING_VERSIONING_STANDARD.md)
- [OTA_STANDARD.md](docs/OTA_STANDARD.md)
- [SAFETY_STANDARD.md](docs/SAFETY_STANDARD.md)
- [TEST_STANDARD.md](docs/TEST_STANDARD.md)
- [WEB_STANDARD.md](docs/WEB_STANDARD.md)

---

# Bezpieczeństwo

Bezpieczeństwo urządzeń sterujących sprzętem fizycznym pozostaje lokalną odpowiedzialnością
domeny. Istniejące projekty mają wybrane lokalne zabezpieczenia, ale wspólny Core
`SafetyManager`/`CommandGuard` nie jest obecnie zaimplementowany. Wspólne guardy i koordynacja
są kierunkiem **TARGET**, opisanym w [SAFETY_STANDARD.md](docs/SAFETY_STANDARD.md).

---

# Alarmy i diagnostyka

Wspólny framework alarmów Core nie jest obecnie zaimplementowany. Model alarmów, ACK,
latch i integracja z safety są kierunkiem **TARGET** opisanym w
[ALARM_STANDARD.md](docs/ALARM_STANDARD.md). Bieżący Core udostępnia ograniczony snapshot
diagnostyczny; zakres CURRENT i docelowy model providerów rozdziela
[DIAGNOSTICS_STANDARD.md](docs/DIAGNOSTICS_STANDARD.md).

---

# Czas i RTC

Urządzenia wymagające harmonogramów wykorzystują lokalny RTC jako źródło czasu, a NTP służy do jego synchronizacji i korekcji.

```text
       Internet
          │
         NTP
          │
          ▼
     synchronizacja
          │
          ▼
       DS3231
          │
          ▼
   lokalna logika
```

Brak Internetu nie może zatrzymać harmonogramów urządzenia.

Core posiada również mechanizmy recovery dla RTC/NTP i obsługę strefy:

```text
Europe/Warsaw
```

wraz ze zmianą czasu lato/zima.

---

# Konfiguracja i storage

Core udostępnia bieżący `StorageService` z dual-slot, CRC, generation, walidacją i weryfikacją
zapisu. Migracje ogólne, backup/import/export i factory reset nie są automatycznie częścią
tego API. Pełny podział CURRENT/TARGET definiuje
[CONFIG_STORAGE_STANDARD.md](docs/CONFIG_STORAGE_STANDARD.md).

---

# Web UI

Urządzenia mogą udostępniać własny lokalny panel WWW.

Core zapewnia transport HTTP, routing i wspólną infrastrukturę, natomiast urządzenie nadal posiada:

* własne strony,
* własne API,
* własną konfigurację,
* własne akcje,
* własne zasady bezpieczeństwa.

W pojedynczym firmware powinien istnieć **jeden fizyczny serwer HTTP**.

```text
                   WebService
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
    Core API      Device API      Device UI
```

Szczegółowy kontrakt transportu i polityki domenowej definiuje
[WEB_STANDARD.md](docs/WEB_STANDARD.md).

---

# MQTT i Home Assistant

MQTT jest opcjonalną warstwą integracji i nie może być źródłem autonomicznej logiki
urządzenia. Wspólny moduł MQTT w Core **nie jest zaimplementowany ani aktywnie wdrażany**;
ma status **LATER**, a spike T0 ESP-MQTT MUSI poprzedzać decyzję transportu i implementację.
Kontrakt docelowy opisuje [MQTT_STANDARD.md](docs/MQTT_STANDARD.md), a kolejność prac
[ROADMAP.md](docs/ROADMAP.md).

---

# Testowanie

Projekt zakłada trzy poziomy weryfikacji:

```text
Unit tests
     ↓
Integration tests
     ↓
Hardware validation
```

Samo:

```text
pio run
```

nie oznacza jeszcze, że funkcja sprzętowa została zweryfikowana.

Zmiany związane m.in. z:

* pompami,
* przekaźnikami,
* PWM,
* RTC,
* Wi-Fi,
* OTA,
* pamięcią,
* bezpieczeństwem,

powinny być sprawdzane również na rzeczywistym sprzęcie.

Pełny standard:

**[docs/TEST_STANDARD.md](docs/TEST_STANDARD.md)**

---

# Aktualny stan ekosystemu

### aquaOneLuma

Projekt referencyjny ekosystemu.

Obsługuje m.in.:

* wielokanałowe PWM,
* profile dobowe,
* płynne przejścia,
* tryby pracy,
* RTC + NTP,
* lokalną konfigurację,
* Web UI,
* diagnostykę,
* integrację z pełnym obecnym zestawem modułów Core.

---

### aquaOneDoser

Sterownik dozownika nawozów.

Aktualnie posiada m.in.:

* obsługę 8 pomp,
* kalibrację,
* harmonogram,
* zapis konfiguracji,
* RTC/NTP,
* MQTT,
* Home Assistant Discovery,
* Web UI,
* integrację z Core prowadzoną etapowo.

Ze względu na istniejącą działającą logikę migracja prowadzona jest bez przepisywania całego projektu naraz.

---

### aquaOneHydro

Sterownik automatycznej dolewki.

Obejmuje m.in.:

* czujnik poziomu akwarium,
* pomiar zapasu wody,
* czujnik ultradźwiękowy,
* pompę dolewki,
* alarmy,
* buzzer,
* lokalne sterowanie,
* panel WWW,
* integrację z wybranymi modułami Core.

---

### aquaOneGas

System monitorowania butli CO₂.

Docelowo:

* pomiar masy butli,
* pomiar ciśnienia,
* pomiar temperatury,
* szacowanie ilości CO₂,
* kontrola poprawności sensorów,
* alarmy,
* Web/MQTT.

Projekt posiada już rozdzieloną strukturę:

```text
app
config
domain
drivers
services
interfaces
```

---

### aquaOneClima

Projekt sterowania warunkami pracy akwarium.

Planowany zakres obejmuje m.in.:

* temperaturę,
* grzanie,
* chłodzenie,
* obieg,
* napowietrzanie,
* CO₂,
* alarmy i zabezpieczenia.

Projekt znajduje się obecnie na etapie szkieletu.

---

### aquaOneFauna

Projekt funkcji związanych z obsadą akwarium.

Pierwszym głównym zastosowaniem jest automatyczny karmnik.

Projekt znajduje się obecnie na etapie szkieletu.

---

# Uruchomienie projektu

Repozytorium jest monorepo PlatformIO.

```bash
git clone https://github.com/pimowo/aquaOne.git
cd aquaOne
```

Następnie otwórz wybrany projekt, np.:

```bash
cd aquaOneLuma
```

Build:

```bash
pio run
```

Upload:

```bash
pio run -t upload
```

Monitor:

```bash
pio device monitor
```

Każdy projekt posiada własny `platformio.ini` oraz własną konfigurację sprzętową.

---

# Zależność od Core

Projekty korzystające z lokalnego Core mogą używać:

```ini
lib_deps =
    symlink://../aquaOneCore
```

Dzięki temu wszystkie projekty w monorepo mogą korzystać z tej samej wersji rozwijanego `aquaOneCore`.

Core jest jednak projektowany tak, aby pozostał niezależną biblioteką techniczną.

---

# Struktura repozytorium

```text
aquaOne/
│
├── aquaOneCore/          # wspólna infrastruktura
│
├── aquaOneLuma/          # oświetlenie
├── aquaOneDoser/         # dozowanie
├── aquaOneHydro/         # dolewka
├── aquaOneGas/           # CO₂
├── aquaOneClima/         # klimat
├── aquaOneFauna/         # karmienie
│
└── docs/
    ├── ARCHITECTURE.md
    ├── PROJECT_MATRIX.md
    ├── ROADMAP.md
    ├── ALARM_STANDARD.md
    ├── SAFETY_STANDARD.md
    ├── MQTT_STANDARD.md
    ├── WEB_STANDARD.md
    ├── OTA_STANDARD.md
    ├── CONFIG_STORAGE_STANDARD.md
    ├── DIAGNOSTICS_STANDARD.md
    ├── TEST_STANDARD.md
    ├── NAMING_VERSIONING_STANDARD.md
     ├── FACTORY_RESET_ONBOARDING_STANDARD.md
     └── IDEAS.md
```

---

# Filozofia rozwoju

Przy rozwijaniu aquaOne obowiązuje kilka prostych zasad:

**Autonomia > integracja**

Urządzenie ma najpierw poprawnie wykonywać swoją funkcję lokalnie.

**Bezpieczeństwo > wygoda**

Zdalne sterowanie nie może omijać zabezpieczeń urządzenia.

**Core ≠ logika akwarium**

Do Core trafiają wyłącznie mechanizmy wspólne technicznie.

**Małe kroki > wielkie migracje**

Działającego projektu nie przepisujemy od zera tylko dlatego, że pojawiła się nowa architektura.

**Test na sprzęcie > samo przejście kompilacji**

Sterujemy realnym sprzętem. Zielony napis `SUCCESS` w PlatformIO nie wie, że pompa właśnie zalała podłogę.

---

## Dokumentacja

Pełna hierarchia dokumentacji i lista aktywnych standardów znajduje się w sekcji
[Documentation / Source of Truth](#documentation--source-of-truth). Najlepsze miejsca do
rozpoczęcia:

* **[Architecture](docs/ARCHITECTURE.md)** — jak zbudowany jest system
* **[Project Matrix](docs/PROJECT_MATRIX.md)** — co faktycznie jest obecnie zaimplementowane
* **[Roadmap](docs/ROADMAP.md)** — dalszy kierunek rozwoju
* **[aquaOneCore](aquaOneCore/README.md)** — dokumentacja wspólnej biblioteki
* **[Ideas](docs/IDEAS.md)** — niezatwierdzone pomysły, nie wymagania ani roadmapa

---

# Status projektu

aquaOne jest aktywnie rozwijanym projektem hobbystycznym.

Architektura i standardy są stopniowo stabilizowane, a istniejące urządzenia migrowane do wspólnej infrastruktury bez utraty ich samodzielności i sprawdzonych funkcji.

```text
Build it.
Test it.
Disconnect Wi-Fi.
It should still work.
```

---

## License

Licencja projektu nie została jeszcze określona.
