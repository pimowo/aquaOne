# Architecture vNext — decyzje

**Status:** DRAFT ACCEPTED dla FAZY 0 i F1.1 CONTRACT GATE
**Scope:** cała platforma aquaOne
**Zasada:** Architecture vNext jest TARGET. CURRENT wynika z kodu i macierzy projektu. Legacy code is not architecture.

## ACCEPTED

### ARCH-001 — autonomia urządzenia
Każde urządzenie działa autonomicznie. Podstawowa funkcja nie zależy od Home Assistant, MQTT, WWW/HTTP, WebSocket/realtime, Panelu, Internetu ani innego aquaOne.

### ARCH-002 — granica Core / Domain
Core dostarcza mechanizmy wspólne, Domain znaczenie funkcjonalne. Core nie zawiera logiki produktowej ani rozgałęzień po `device_type`.

### ARCH-003 — równorzędni klienci
Luma, Doser, Hydro, Clima, Gas i Fauna są równorzędnymi klientami platformy. Panel jest klientem, nie zależnością krytyczną domeny.

### ARCH-004 — Composition Root
Composition Root zna konkretny skład urządzenia, hardware, BoardProfile, Core i adaptery. Domain otrzymuje jawne, wąskie zależności.

### ARCH-005 — ocena legacy
Istniejący kod może zostać oceniony jako KEEP, ADAPT, REWRITE albo REMOVE. Nie zmieniamy Architecture vNext tylko po to, aby zachować kompatybilność z istniejącym projektem.

### ARCH-006 — Composition Root i ApplicationRuntime
Composition Root zna konkretne implementacje i składa aplikację. ApplicationRuntime wyłącznie
koordynuje startup i runtime przez wąskie kontrakty. Nie posiada konkretnych usług, nie zna
konkretnych klas Domain, Network, Web ani MQTT, nie jest service locatorem ani Registry i nie
zawiera semantyki domenowej. Dokładny ApplicationPlan, hooks i ownership pozostają DECISION
REQUIRED.

### SYS-001 — lifecycle
Docelowy lifecycle to: POWER ON, BOOT, CORE INIT, LOAD/VALIDATE CONFIG, HARDWARE INIT, DOMAIN INIT, SAFETY VALIDATION, NETWORK INIT, INTERFACES INIT, RUNNING.

### SYS-002 — niezależne osie stanu
OperationalState, HealthState, SafetyState, DomainMode, alarms i Action Locks są rozdzielone. Nie tworzymy jednego ogromnego enum.

### SYS-003 — StartupPhase
Publiczny `StartupPhase` opisuje wyłącznie startup: `BOOT`, `CORE_INIT`,
`LOAD_VALIDATE_CONFIG`, `HARDWARE_INIT`, `DOMAIN_INIT`, `SAFETY_VALIDATION`,
`NETWORK_INIT`, `INTERFACES_INIT`, `RUNNING`. Nie obejmuje Maintenance, Recovery ani
Restart i nie konkuruje z OperationalState. Early safe outputs są technicznym krokiem na
początku `BOOT`, a nie osobną publiczną fazą.

### SYS-004 — wynik startupu
`StartupRequirement` rozróżnia `REQUIRED` i `OPTIONAL`, a `StartupOutcome` rozróżnia
`SUCCEEDED`, `DISABLED` i `FAILED`. Requirement należy do composition/deklaracji
participanta, nie do wyniku operacji. `REQUIRED + FAILED` zatrzymuje normalny startup i
prowadzi do `ERROR + FAULT + LOCKED`. `OPTIONAL + FAILED` nie blokuje startupu i może
prowadzić do `DEGRADED`. `OPTIONAL + DISABLED` nie degraduje health. `REQUIRED + DISABLED`
jest kombinacją nieprawidłową.

### SYS-005 — stabilny startup error code
Każdy startup failure posiada krótki, stabilny error code. Długi dynamiczny tekst nie jest
podstawowym kontraktem błędu. Dokładna reprezentacja, szerokość i katalog kodów pozostają
DECISION REQUIRED.

### SYS-006 — stan systemu w Fazie 1
`OperationalState` ma wartości `BOOTING`, `RUNNING`, `MAINTENANCE`, `ERROR`;
`HealthState`: `OK`, `DEGRADED`, `FAULT`; `SafetyState`: `CLEAR`, `LOCKED`.
ApplicationRuntime jest authoritative writer dla OperationalState. HealthState wynika w
Fazie 1 z minimalnej koordynacji startup/runtime. SafetyState startuje jako `LOCKED`, a
startup safety gate może przejść do `CLEAR`. Nie powstaje ogólne `setSafetyState()`,
SafetyManager ani Action Locks. `MAINTENANCE` istnieje w typie, ale jego workflow należy do
późniejszej fazy.

### SYS-007 — granice restartu
`RestartReason` opisuje, dlaczego urządzenie wystartowało, `RestartRequestReason` — dlaczego
bieżący runtime żąda restartu, a `RestartExecutor` wykonuje efekt platformowy. Domain i
transport otrzymują wyłącznie `RestartRequester`. Zakres Fazy 1 to: request, pending request,
safe point w runtime loop, RestartExecutor. RestartPreparation, Maintenance transition,
MQTT offline, timeouty i OTA workflow pozostają poza Fazą 1.

### IDN-001 — rozdzielenie identity
`DeviceIdentity`, `BuildIdentity`, `HardwareIdentity` i `RuntimeIdentity` są osobnymi
pojęciami. Friendly/user-visible name nie jest częścią technical identity. Dokładne pola,
format `device_id`, użycie MAC/MAC6, capacities oraz kontrakt RuntimeIdentity pozostają
DECISION REQUIRED.

### CMD-001 — wspólna ścieżka komendy
Każde źródło sterowania korzysta z Source → Command → Validation → Authorization/Policy → Safety/Action Locks → Domain execution → State update → Event/Result.

### EVT-001 — snapshot jest źródłem prawdy
Snapshot jest autorytatywnym stanem. Realtime i Event informują o zmianach, ale nie zastępują snapshotu.

### EVT-002 — resync
W V1 nie ma event replay. Po reconnect, reboot albo wykryciu niespójności klient wykonuje pełny resync.

### CFG-001 — rozdział konfiguracji
CoreConfig, DomainConfig, DomainState, SystemState i RuntimeState są rozdzielone. RuntimeState nie jest persistent.

### CFG-002 — lifecycle konfiguracji
Konfiguracja przechodzi przez load, decode, version, migrate, validate i apply. Walidacja poprzedza zapis i zastosowanie.

### SAF-001 — rozdział Safety/Maintenance/Action Lock
Maintenance, Safety i Action Lock są różnymi mechanizmami. SafetyState jest agregatem, nie drugim systemem blokad.

### ALM-001 — rozdział pojęć alarmowych
Warning, alarm, fault i safety lock są różne. ACK nie oznacza CLEAR, a alarm nie oznacza automatycznie Safety Lock.

### HW-001 — trzy poziomy hardware
Rozdzielamy generic technical abstractions, concrete hardware drivers i domain hardware interfaces. BoardProfile należy do projektu.

### HW-002 — EarlySafeOutputInitializer
`EarlySafeOutputInitializer` wykonuje przed odczytem configu i pełnym hardware init
idempotentną techniczną operację ustawienia konserwatywnego stanu fizycznych wyjść. Nie
interpretuje alarmów ani Safety policy i nie należy do przyszłego Safety framework. Raportuje
jawny sukces albo błąd. Urządzenie bez ryzykownych wyjść może użyć neutralnej implementacji
no-op, bez rozgałęzień po `device_type`.

### WEB-001 — jeden transport
Jedno urządzenie ma jeden fizyczny transport Web. HTTP i Realtime docelowo współdzielą backend i port.

### SEC-001 — Auth i Safety
Auth odpowiada za uprawnienie, Safety za możliwość wykonania akcji w danym stanie. Domain nie zna haseł, sesji ani handshake transportu.

### REG-001 — Feature Registration
Capability/providers są rejestrowane jawnie podczas startup. Registry nie jest service locatorem. Provider nie może utrzymywać konkurencyjnej kopii Domain State.

### SEC-002 — ochrona sekretów
Sekrety nie mogą trafiać do status, diagnostics, logs, Realtime ani MQTT state.
### DIAG-001 — rozdział diagnostyki
CoreDiagnostics i DomainDiagnostics są semantycznie oddzielone i korzystają ze wspólnej infrastruktury.

## DECISION REQUIRED

- ARCH-101 — dokładny API Core ↔ Domain;
- SYS-101 — nazwa, algorytm, encoding, generator, RNG source i collision policy RuntimeIdentity;
- SYS-102 — dokładny ApplicationPlan, hooks, liczba participantów oraz ownership pointer/reference;
- SYS-104 — dokładny wrapper wyniku startupu, StartupReport oraz reprezentacja i katalog startup error codes;
- SYS-105 — recovery po krytycznym błędzie startupu;
- SYS-106 — dokładny przyszły writer ownership dla HealthState i SafetyState;
- SYS-107 — dokładny katalog RestartRequestReason i zachowanie wielu pending requestów;
- IDN-101 — pola identity, format device_id, MAC/MAC6, capacities i zasady walidacji;
- CFG-101 — dokładny model pending config i recovery po korupcji;
- CFG-102 — zakres DomainState w backupie;
- CMD-101 — command envelope, CommandResult, error_code, request/correlation ID;
- CMD-102 — idempotency i deduplication;
- EVT-101 — event envelope, priority i sequence format;
- EVT-102 — snapshot schema oraz relacja snapshot/HTTP/realtime;
- ALM-101 — wspólny API alarmów i zakres capability;
- SAF-101 — dokładny model Action Lock i priorytet powodów;
- MNT-101 — kontrakt przygotowania domeny do maintenance;
- DIAG-101 — lista providerów/capability diagnostycznych;
- REG-101 — nazwy providerów, API registry i limity;
- WEB-101 — public/auth policy endpointów;
- WEB-102 — API HTTP i kompatybilność z obecnym Web Core;
- RT-101 — protokół realtime, auth, heartbeat i reconnect;
- MQTT-101 — zakres wspólnej infrastruktury MQTT;
- SEC-101 — auth HTTP/WebSocket oraz model sekretów;
- HW-101 — polityka wspólnych driverów i dokładne kontrakty capability;
- MNT-102 — zakres wspólnego OTA, restartu, backup/restore i factory reset;
- PANEL-101 — protokół i zakres klienta aquaOne Panel;
- SYS-103 — onboarding/recovery mode.

Nie ustalamy tych wartości na podstawie istniejącego Dosera ani innego kodu legacy.

## SPIKE REQUIRED

- WEB/RT: HTTP + WebSocket na jednym porcie i jednym transporcie;
- WEB/RT: HTTP podczas aktywnego WebSocket, reconnect, full resync, slow client i backpressure;
- WEB/RT: kompatybilność ESP32 i ESP32-S3, heap, flash i wpływ na main loop;
- WEB/OTA: multipart OTA, abort, auth handshake oraz reconnect po restart/OTA;
- RT: liczba klientów, heartbeat, kolejki, payload size i timing — wyłącznie z pomiarów;
- CFG: atomic persistence i recovery po utracie zasilania;
- REG: koszt serializacji, provider limits i zachowanie przy pełnym registry;
- TEST: runtime evidence na fizycznym hardware dla wybranych scenariuszy.

Nie wpisujemy do standardu konkretnych limitów ani timeoutów bez pomiarów.