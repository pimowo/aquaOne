# Roadmap — Plan prac ekosystemu aquaOne

Roadmap opisuje wyłącznie stan przyszłych prac. Aktualny stan implementacji znajduje się w
[PROJECT_MATRIX.md](PROJECT_MATRIX.md), a kontrakty w odpowiednich `*_STANDARD.md`.

## Kolejka wykonawcza

### DONE

- AquaCore System, Config/Storage, Logging, Diagnostics, Network, Web i Time;
- Doser: migracje Network, Storage/Config i Time przez adaptery;
- Web W1: neutralny transport i jeden fizyczny backend HTTP;
- Web W1.5: restart i OTA Dosera. Hardware validation: **PASSED** 2026-09-12.
	Evidence: not yet persisted in repository.

### NEXT

1. Zatwierdzenie dokumentacji jako source of truth.
2. Utrwalenie w repozytorium raportu hardware validation W1.5 zgodnego z
	[TEST_STANDARD.md](TEST_STANDARD.md), bez rekonstruowania nieistniejących danych.
3. Wykonanie focused Web test jako runtime test i rozwiązanie rozbieżności: fixture wywołuje
	`WebService::begin()`, a test oczekuje braku trasy `/`, mimo że Core ją rejestruje.
4. W2 Web Dosera zgodnie z [WEB_STANDARD.md](WEB_STANDARD.md).
5. Testy regresyjne i sprzętowe po każdym kroku W2.

### LATER

- spike T0 natywnego ESP-MQTT przed implementacją wspólnego Core MQTT;
- minimalny read-only adapter HA Dosera po pozytywnym T0;
- wspólny mechanizm alarmów, OTA/rollback i onboarding dopiero po zatwierdzeniu standardów.

### BACKLOG

- opcjonalne integracje Core w Hydro i Gas;
- rozwój domen Clima i Fauna;
- elementy MQTT `text` i `time`.

Poniższe fazy zachowują kontekst zakresu. Status tekstowy `DONE`, `NEXT`, `LATER` lub
`BACKLOG` jest rozstrzygający.

## Faza 1: Dokumentacja i kontrakt (NEXT)

**Cel:** Utrwalić architekturę, kontrakt API, standard projektów.

- ✅ Audyt Core (NTP, RTC recovery, Storage, Network, Web)
- ✅ Pełna analiza porównawcza (Core vs Doser)
- ✅ Dokumentacja Architecture.md, PROJECT_MATRIX.md, ROADMAP.md
- ✅ Definicja standard projektu (struktura katalogów, separacja warstw)
- ✅ Weryfikacja EuropeWarsawTimeService (utrzymany w Core)

**Dostarczenia:**
- `aquaOne/README.md` — quickstart
- `aquaOne/docs/ARCHITECTURE.md` — design decisions
- `aquaOne/docs/PROJECT_MATRIX.md` — status matrix
- `aquaOneCore/README.md` — Core API documentation

**Ryzyko:** Brak
**Status:** NEXT — aktualizacja i akceptacja pełnego zestawu standardów.

---

## Faza 2: Gas — Czysty klient Core (BACKLOG)

**Cel:** Pierwszy projekt (po Luma) który w pełni integruje Core.

1. GasSenseApp::begin() — faktycznie inicjalizuje Core
2. Config + Storage (jeśli potrzebne do persistencji konfiguracji)
3. Network + Web (jeśli potrzebne do dostępu do UI)
4. Diagnostics snapshot w Web UI (jeśli potrzebne)

**Wymagania:**
- Dokumentacja Core gotowa (Faza 1)
- Gas kod review (architektura już OK)

**Dostarczenia:**
- aquaOneGas v1.0-alpha (Core integration)
- Test report

**Ryzyko:** Niskie
**Status:** BACKLOG

---

## Faza 3: Hydro — Opcjonalne dodatki (BACKLOG)

**Cel:** Wzbogacić Hydro o Logger (opcjonalnie).

1. Dodać AquaCore::Logger do HydroSenseApp (jeśli to się przydaje)
2. Weryfikacja bieżącej integracji bez zmian kodu logiki

**Wymagania:**
- Hydro już działa dobrze
- Logger nie zmieni logiki

**Dostarczenia:**
- aquaOneHydro v2.1 (Logger integration) — opcjonalnie

**Ryzyko:** Bardzo niskie (additive)
**Status:** BACKLOG

---

## Faza 4: Doser — Migracja do Core (NEXT)

**Cel:** Doser przechodzi na Core bez zepsucia działania.

### Etap 4a: Network migration — DONE
Lokalny `WiFiManager` działa jako adapter nad `NetworkService` i `Esp32NetworkBackend`.

### Etap 4b: Storage/Config migration — DONE
`StorageManager` używa Core `StorageService` i zachowuje migrację danych legacy.

### Etap 4c: Time migration — DONE
`TimeManager` pozostaje adapterem domenowym nad usługami czasu Core.

### Etap 4d: Web migration
1. **W1 — DONE:** `WebService` + `Esp32WebBackend` jako jedyny fizyczny serwer HTTP.
2. **W1.5 — DONE:** restart/OTA, auth, success/failure/abort, cleanup i reconnect.
	Hardware validation: **PASSED** 2026-09-12. Evidence: not yet persisted in repository.
3. **W2 — NEXT:** pozostałe strony i API produktu zgodnie z WEB_STANDARD.

### Etap 4e: Cleanup — LATER
1. Potwierdzić po W2, że nie powstał duplikat transportu ani legacy ownership fizycznego
	`WebServer`; usunąć wyłącznie konkretny znaleziony duplikat. W1 już zapewnia jednego
	właściciela serwera, więc nie jest to ponowne planowanie W1.
2. Zachować w Doserze właściciela polityki restartu/OTA, maintenance, pump stop, domain
	effects i restart sequencing. Ewentualna zmiana nazwy `WebManager` jest osobnym refactorem.
3. Zweryfikować, że MQTT, diagnostics i scheduler nadal działają.
4. Doser v2.0 (Core integration).

**Wymagania dla końcowego Doser v2.0:**
- zakończone i zaakceptowane etapy migracji Dosera;
- stabilne używane moduły Core;
- reverse-commit strategy i test points.

Faza 2 Gas nie jest bramą W2.

**Dostarczenia:**
- aquaOneDoser v2.0 (Core migration)
- Migration notes

**Ryzyko:** Wysokie (duża zmiana)
**Strategy:** Reverse-commit na każdym etapie; hardware tests
**Status:** NEXT

---

## Faza 5: Luma — Bez zmian (BACKLOG)

**Cel:** Luma pozostaje referencją bez dużych zmian.

Opcjonalnie:
- Update dokumentacji (jeśli zmieni się Core API)
- Performance tuning (jeśli potrzebna)
- Nowe funkcje domeny (nie zmiany Core)

**Ryzyko:** Brak (no changes)
**Status:** BACKLOG

---

## Faza 6: Clima — Nowy projekt ze standardem (BACKLOG)

**Cel:** Clima buduje się od razu ze standardem.

1. Wzorzec: struktura jak Hydro/Gas
2. Zdefiniować domenę (temp control, cycles, alarms)
3. Integracja Core: wyłącznie potrzebne moduły
4. Test

**Wymagania:**
- Projekt architektoniczny
- Standard proyecto gotowy (Faza 1)

**Dostarczenia:**
- aquaOneClima v0.1-alpha

**Ryzyko:** Niskie (świeży projekt)
**Status:** BACKLOG

---

## Faza 7: Fauna — Nowy projekt ze standardem (BACKLOG)

**Cel:** Fauna buduje się od razu ze standardem.

Podobnie jak Clima.

**Wymagania:**
- Projekt architektoniczny
- Standard proyecto gotowy (Faza 1)

**Dostarczenia:**
- aquaOneFauna v0.1-alpha

**Ryzyko:** Niskie (świeży projekt)
**Status:** BACKLOG

---

## Faza 8: MQTT i Home Assistant (LATER)

**Cel:** Dodać integrację MQTT/HA do Core.

Składowe:
- MqttService + MqttBackend
- T0: spike linkowania i lifecycle natywnego ESP-MQTT na ESP32/ESP32-S3
- Esp32MqttBackend (ESP-MQTT za neutralnym interfejsem; decyzja finalna po T0)
- HA discovery helpers
- Entity registration mechanism
- Minimalny read-only adapter Dosera: 5 encji na pompę + `automatic_dosing`

**Wymagania:**
- dokumentacja source of truth zaakceptowana;
- W2 Dosera zakończone i zwalidowane;
- regresja i HIL Dosera zakończone;
- stabilny baseline używanych modułów Core.

Gas, Hydro, Clima i Fauna nie są bramą dla T0 ani Core MQTT.

**Kolejność:**
1. T0 ESP-MQTT;
2. decyzja transportu na podstawie T0;
3. implementacja Core MQTT;
4. minimalistyczny read-only adapter Dosera.

**Dostarczenia:**
- Raport T0 z QoS 1/PUBACK, reconnect, LWT, RAM i Flash
- AquaCore with MQTT support
- MQTT integration guide

**Ryzyko:** Medium
**Status:** LATER; T0 jest pierwszym krokiem i MUSI poprzedzać implementację Core MQTT.
`text` i `time`: BACKLOG. MQTT_STANDARD zachowuje dla nich kontraktowe oznaczenie `CAN WAIT`.

---

## Faza 9: Wspólne OTA i Watchdog (LATER)

**Cel:** Dodać OTA firmware updates i watchdog recovery.

Składowe:
- OtaService + OtaBackend
- Rollback safety
- Watchdog / restart recovery

**Wymagania:**
- MQTT stable (Faza 8, jeśli potrzebne)
- Core stable

**Dostarczenia:**
- AquaCore with OTA support

**Ryzyko:** Medium (critical function)
**Status:** LATER. Doser W1.5 ma lokalne OTA; wspólny Core OTA, signing i rollback nie są
zaimplementowane.

---

## Timeline — Orientacyjne (ELASTYCZNE, BEZ TERMINÓW)

```
Faza 1 (Dokumentacja)   NEXT
Faza 2 (Gas)            BACKLOG
Faza 3 (Hydro)          BACKLOG
Faza 4 (Doser)          NEXT
Faza 5 (Luma)           BACKLOG
Faza 6 (Clima)          BACKLOG
Faza 7 (Fauna)          BACKLOG
Faza 8 (MQTT)           LATER
Faza 9 (OTA)            LATER
```

---

## Kryteria sukcesu każdej fazy

| Faza | Kryterium |
|------|-----------|
| 1 | Dokumentacja complete, brak open questions |
| 2 | Gas integrated, tests passing, zero regression |
| 3 | Hydro enhanced, optional Logger, no regression |
| 4 | Doser migrated, all functionality preserved, hw tests OK |
| 5 | Luma stable, no regression |
| 6 | Clima running, tests passing |
| 7 | Fauna running, tests passing |
| 8 | MQTT stable, entity registration working |
| 9 | OTA updates working, recovery tested |

---

## Metryki do śledzenia

- **Build time** — Nie powinna rosnąć (Core size)
- **Flash usage** — Per-device breakdown
- **Regression** — Changelog dla każdego projektu
- **Migration risk** — Test points dla Dosera
