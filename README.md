# aquaOne — Ekosystem autonomicznych urządzeń dla akwariów

aquaOne to rodzina niezależnych, samodzielnych urządzeń IoT przeznaczonych dla akwariów, systemów hodowlanych i kontroli środowiska. Każde urządzenie wykonuje swoją funkcję bez zależności od innych modułów, Internetu, MQTT czy Home Assistant.

## 🎯 Główne założenie architektury

- **Autonomia:** Każde urządzenie steruje swoją domeną niezależnie
- **Niezawodność:** Awaria Wi-Fi, MQTT, HA lub innego urządzenia nie zatrzymuje pracy podstawowej funkcji
- **Brak zależności kodowych:** Projekty urządzeń nie importują kodu od siebie
- **Wspólna infrastruktura:** Techniczne funkcje trafiają do `aquaOneCore`
- **Opcjonalność:** Każde urządzenie używa tylko potrzebnych modułów Core
- **Logika lokalna:** Algorytmy, sterowniki, obsługa domeny pozostają w urządzeniach

## 📦 Moduły ekosystemu

| Moduł | Status | Funkcja |
|-------|--------|---------|
| **aquaOneLuma** | 🟢 Stabilny | Oświetlenie LED, dzień/noc |
| **aquaOneDoser** | 🟡 Funkcjonalny | Dozownik nawozów, scheduling |
| **aquaOneHydro** | 🟢 Funkcjonalny | Dolewka, pomiar poziom wody |
| **aquaOneGas** | 🟡 Budowa | Pomiar CO2, ciśnienie, temperatura |
| **aquaOneClima** | 🔴 Planowany | Klimat, temperatura, obieg |
| **aquaOneFauna** | 🔴 Planowany | Karmnik, funkcje zwierzęce |
| **aquaOneCore** | 🟢 Fundament | Wspólne usługi techniczne |

## 🏗️ Architektura

Każde urządzenie buduje się w podobnej strukturze:

```
aquaOneXxx/
├── platformio.ini           # Konfiguracja PlatformIO, zależności
├── include/
│   ├── BuildConfig.h        # GPIO pins, debug flags
│   ├── NetworkSecrets.h     # WiFi credentials (gitignored)
│   ├── XxxConfig.h          # Konfiguracja produktu
│   └── app/XxxApp.h         # Composition root
├── src/
│   ├── main.cpp             # Minimal: setup() → app.begin(), loop() → app.update()
│   ├── app/XxxApp.cpp       # Inicjalizacja, orkiestracja
│   ├── domain/              # Logika biznesowa
│   ├── drivers/             # Sterowniki sprzętu
│   ├── services/            # Algorytmy, helpery
│   └── interfaces/          # Web, MQTT (pluggable)
└── docs/
    ├── ARCHITECTURE.md      # Model domenowy
    ├── CONFIG_SCHEMA.md     # Schemat konfiguracji
    └── HARDWARE.md          # GPIO, pinout
```

## 🔌 Moduły aquaOneCore

Core dostarcza abstrakcje dla funkcji technicznych, wspólnych dla wszystkich urządzeń:

- **System** — boot status, device identity, restart reasons
- **Config** — persistent storage z CRC32, versioning, dual-slot safety
- **Time** — RTC (DS3231) + NTP synchronization + Europe/Warsaw timezone
- **Network** — WiFi (STA + AP modes), state machine, reconnect logic
- **Web** — HTTP server, routing, provider pattern dla custom routes
- **Logging** — Unified logger z pluggable sinks, compile-time control
- **Diagnostics** — Agregator statusów modułów

## ⚙️ Brak zależności kodowych

```
aquaOneLuma ─┐
aquaOneDoser─┼─→ aquaOneCore (optional modules)
aquaOneHydro─┼
aquaOneGas ──┘

X aquaOneLuma importuje aquaOneDoser code — NIGDY!
X aquaOneDoser importuje aquaOneHydro code — NIGDY!
```

Każdy projekt ma własny composition root, własną konfigurację, własną logikę. Core to biblioteka, nie framework narzucający strukturę.

## 🚀 Quick Start — Nowy projekt

```bash
# 1. Skopiuj strukturę z istniejącego projektu (np. aquaOneHydro)
cp -r aquaOneHydro aquaOneXxx
cd aquaOneXxx

# 2. Edytuj konfigurację
nano include/BuildConfig.h      # Piny GPIO
nano include/XxxConfig.h        # Parametry produktu
nano platformio.ini             # Platforma, zależności

# 3. Zaimplementuj domenę
# src/app/XxxApp.h/cpp + src/domain/* + src/drivers/*

# 4. Testuj
pio run -e esp32-s3 -t upload
```

Dokumentacja: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [aquaOneCore/README.md](aquaOneCore/README.md)

## 📋 Konwencje

- **Nazwy klas domenowych:** `PascalCase` (np. `PumpManager`, `LightEngine`)
- **Zmienne członkowskie:** `camelCase_` (koniec: underscore)
- **Stałe:** `SCREAMING_SNAKE_CASE`
- **Namespace:** Device-specific (np. `LumaSense::`, `gassense::`)

## 📚 Dokumentacja

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — Szczegółowa architektura systemu
- [docs/PROJECT_MATRIX.md](docs/PROJECT_MATRIX.md) — Status każdego projektu
- [docs/ROADMAP.md](docs/ROADMAP.md) — Plan prac i etapy integracji
- [aquaOneCore/README.md](aquaOneCore/README.md) — API modułów Core

## 🔄 Autonomia i Recovery

Każde urządzenie:

1. **Startuje niezależnie** — nie czeka na inne
2. **Działa bez sieci** — podstawowa funkcja niezależna od WiFi
3. **Odzyskuje się po błędach** — retry logic, timeouts, fallbacks
4. **Loguje problemy** — via AquaCore::Logging (opcjonalne)
5. **Raportuje diagnostykę** — via Web (opcjonalne)

## 📄 License

[TBD]

## 🤝 Contribution

1. Feature branch z testami
2. PR z opisem zmian
3. Code review
4. Merge po teście sprzętowym
