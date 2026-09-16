# Architecture vNext — decyzje

**Status:** DRAFT ACCEPTED dla FAZY 0
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

### SYS-001 — lifecycle
Docelowy lifecycle to: POWER ON, BOOT, CORE INIT, LOAD/VALIDATE CONFIG, HARDWARE INIT, DOMAIN INIT, SAFETY VALIDATION, NETWORK INIT, INTERFACES INIT, RUNNING.

### SYS-002 — niezależne osie stanu
OperationalState, HealthState, SafetyState, DomainMode, alarms i Action Locks są rozdzielone. Nie tworzymy jednego ogromnego enum.

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
- SYS-101 — nazwy i kontrakt boot_id/runtime_id;
- SYS-102 — dokładny model Application lifecycle;
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

Nie ustalamy tych wartości w FAZIE 0 na podstawie istniejącego Dosera.

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