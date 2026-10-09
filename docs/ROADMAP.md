# Roadmap — Architecture vNext

Roadmap opisuje kolejność budowy platformy docelowej. `CURRENT` oznacza kod istniejący,
`TARGET` architekturę vNext, `REQUIRED NEXT` najbliższy wymagany krok, a `FUTURE` późniejszy
kierunek. Istniejące domeny nie definiują architektury platformy.

## FAZA 0 — Formalizacja Architecture vNext (CURRENT: DONE)

- formalny model autonomii i granic Core/Domain;
- Composition Root, dependency injection i jawne capability;
- model lifecycle, stanu, Command Path, Snapshot/Event/Realtime;
- rozdział Config/Storage, Safety/Maintenance/Action Lock i alarmów;
- decyzje ACCEPTED, DECISION REQUIRED i SPIKE REQUIRED;
- rozdzielenie CURRENT od TARGET bez zmian w kodzie produkcyjnym.

## Kolejność docelowa

1. **FAZA 1 — System / Lifecycle / Identity / Core↔Domain foundation (CLOSED)**: dostarczony system/runtime state model, startup plan/result/report, ApplicationRuntime startup foundation, recovery semantics, runtime Health/Safety ownership, restart request policy, RuntimeIdentity TARGET, DeviceIdentity TARGET, Core↔Domain TARGET boundary oraz native System test foundation. Zaakceptowane kontrakty TARGET nie oznaczają pełnej implementacji CURRENT.
2. **FAZA 2 — Logging / Storage / Config (CLOSED)**: dostarczone logging, storage foundation i neutralny config lifecycle z recovery integration.
3. **FAZA 3 — Commands / Safety (CLOSED)**: wspólna ścieżka komend, policy i Action Locks.
4. **FAZA 4 — Events / Alarms (CLOSED)**: dostarczono CURRENT foundation snapshotów, eventów i lifecycle alarmów.
5. **FAZA 5 — Maintenance (CLOSED)**: dostarczono foundation legalnych i idempotentnych przejść `RUNNING ↔ MAINTENANCE`, synchronicznego borrowed participant oraz `ApplicationRuntime` orchestration; tylko `Prepared` zatwierdza stan docelowy, a `Failed` lub nielegalny wynik prowadzi do `ERROR` i sticky `FAULT + LOCKED`. Dostarczono też osobną Maintenance command policy, normal processing wyłącznie w `RUNNING` z intrinsic safety poza gate, ulotny stan Maintenance i granicę restart-required jako osobnego system concern.
6. **FAZA 6 — Diagnostics / Registry (CLOSED)**: dostarczono typed diagnostics foundation i statyczny registry do enumeracji.
7. **FAZA 7 — Time / Network adaptation (CLOSED)**: dostarczono CURRENT monotonic i wall clock foundation, RTC jako offline UTC source, opcjonalną synchronizację NTP oraz optional Network startup, runtime i live Health.
8. **FAZA 8 — HTTP + WebSocket feasibility spike (CLOSED)**: F8.1–F8.3C zamknęły feasibility i wybrały `esp_http_server` jako WEB-103 ACCEPTED — TARGET dla przebadanej bazy; production Web nie został zmigrowany.
9. **FAZA 9 — Production Web + Realtime (CLOSED)**: Core native HTTP/Realtime foundation, F9.6 classic ESP32 HIL oraz produkcyjne migracje HTTP Luma, Hydro i Doser są zakończone. Produkty pozostają bez kompozycji WS. Końcowy checkpoint: F9.8B.
10. **FAZA 10 ? Home Assistant Integration (IN PROGRESS: F10.1A CLOSED; F10.1B CLOSED/CURRENT; F10.2 CLOSED/CURRENT; F10.3 REQUIRED NEXT)**: custom integration `aquaOne`, local HTTP API client, future WebSocket/Realtime client, Config Flow, Zeroconf/mDNS and product-aware entity mapping. F10.1B publishes stable identity and API protocol version. F10.2 delivered the read-only Luma integration, confirmed by real HA HIL. Next: F10.3 Realtime reconnect/resync; F10.4 Luma commands; F10.5 Hydro adapter; F10.6 Doser MQTT replacement. F10.3 requires prior review of the product WS/cohort/public wire contract. Decision: [F10.1A](HA_F10_1A_ARCHITECTURE.md); evidence: [F10.2 HIL](HA_F10_2_LUMA_READONLY_HIL.md).
11. **FAZA 11 — OTA / Backup / Restore / Factory Reset (FUTURE)**: wspólne workflow i recovery.
12. **FAZA 12 — UI Shell (FUTURE; foundation częściowo CURRENT)**: wspólny shell/design system po stabilizacji kontraktów. Shared Web Theme v1 jest już CURRENT foundation, ale nie zamyka całej fazy.
13. **FAZA 13 — Reference Empty Device (FUTURE)**: minimalny klient weryfikujący platformę bez domeny.
14. **FAZA 14 — First real Domain migration (FUTURE)**: wybór projektu dopiero po gotowej platformie.

## Obecny stan prac

W repozytorium istnieją używane moduły System, Config/Storage, Logging, Diagnostics,
Network, Web i Time. Core ma natywny HTTPD z opcjonalną WS capability i spójnym
Realtime resync; Luma, Hydro i Doser używają natywnego HTTP w produkcji, bez produktowego WS.
Doser, Luma i Hydro są klientami CURRENT do późniejszej oceny, a nie wzorcem Architecture
vNext. Fundamenty Commands/Safety (Phase 3), Events/Alarms (Phase 4), Maintenance (Phase 5)
oraz Diagnostics/Registry (Phase 6) są dostępne CURRENT: typed `DiagnosticProvider<Snapshot>` z
caller-owned snapshots i semantyką `Success` / `Unavailable`, read-only Core projections,
Domain diagnostics boundary bez duplicate authority oraz statyczny `DiagnosticRegistry<Entry>`
do deterministycznej enumeracji caller-owned immutable entries, bez runtime registration,
service locatora ani heterogeneous provider dispatch. Konkretne Maintenance operations,
per-operation Safety i ActionLock policy, wielu participantów, Maintenance Events/Diagnostics,
pełny RestartRequester/SYS-107 workflow oraz domain/HIL behavior pozostają TARGET/FUTURE. Pełne rozszerzenia Events,
w tym envelope, transport projection, reconnect/resync, serializacja i replay/history, oraz
alarm registry, severity, physical persistence schema, history/counters, bogatsza diagnostyka
i transport również pozostają TARGET/FUTURE. Descriptor schema, stable/global diagnostic IDs,
transport visibility/projection, timestamp conventions, richer diagnostic/failure metadata,
hardware-specific diagnostics, migracja legacy `DiagnosticsService` i real Domain migration
pozostają TARGET/FUTURE. Produktowa kompozycja Realtime, Home Assistant integration,
wspólne OTA i Backup/Restore również pozostają TARGET/FUTURE; Core Realtime jest CURRENT,
a Doser ma lokalne produkcyjne OTA. F10.1B server identity/API jest CURRENT dla obecnego
zakresu ESP32-family; F10.2 pozostaje najbliższym wymaganym krokiem integracji HA.

Phase 7 CURRENT rozdziela monotonic timing od UTC `WallClock`: prawidłowy RTC działa offline,
NTP tylko opcjonalnie synchronizuje RTC, a Network pozostaje opcjonalną infrastrukturą bez
automatycznego wpływu na Safety lub autonomiczną pracę Domain. NET-101 pozostaje PARTIALLY
ACCEPTED: AP fallback, static IP/DNS, bogatszy reconnect/backoff, Network events/diagnostics
i Internet reachability są późniejszym zakresem, który nie blokuje zamknięcia Phase 7.
Znany dług legacy: `test_network` ma nieaktualną fixture `StorageService`, adaptery czasu
`uint32_t millis()` wymagają obserwacji co najmniej raz na pełny cykl licznika, a ESP32
`Esp32NetworkBackend::activeInstance_` obsługuje jedną aktywną instancję. Semantyka
`applyRadioPolicy() == false` została rozstrzygnięta w F7.5.

## F9.7F1 — Doser native Web gate

F9.7F1/F2 and F9.7G1–G5 are CLOSED. G4 atomically replaced the legacy Doser Web
listener with one `EspIdfWebTransport` + `NativeWebService`; native streaming
`POST /update` is CURRENT. G5 real OTA and 300 s bare-board ESP32-S3 HIL passed
without source/test changes. Pumps and output stages were absent, so physical dosing
was not tested. Doser MQTT/Discovery remains LEGACY CURRENT until F10.6; at the
F9.7F1 checkpoint Phase 10 had not started. Product-local OTA is transitional until Phase 11 common OTA.
SEC-101 remains OPEN and SYS-107 remains OPEN/transitional. Evidence and limits:
docs/WEB_F9_7G1_DOSER_OTA_DESIGN.md.

## F9.8A — Phase 9 closure audit

F9.8A wybrał **A — PHASE 9 CLOSABLE NOW** na podstawie zaakceptowanego zakresu
F9.1–F9.7 i F9.7A: produkcyjny WS w konkretnym produkcie nie był warunkiem
wyjścia z Phase 9. Core WS/resync jest CURRENT i przeszedł F9.6 HIL; żaden z
trzech produktów nie ma obecnie endpointu WS ani koherentnej kohorty resync.
F9.8B zamknął Phase 9. SEC-101, WEB-101 i pozostały RT-101 pozostają otwarte,
bez zmiany przyjętego zakresu wyjściowego. Phase 10 rozpoczęła się później od dokumentacyjnej bramki F10.1A; implementacja integracji nie rozpoczęła się.
Szczegóły i granice: docs/WEB_F9_8B_PHASE9_CLOSURE.md.

## Zasady bramki

Nie rozpoczynamy migracji domeny przed zakończeniem odpowiednich kontraktów platformy.
Nie wpisujemy limitów, timeoutów ani formatów protokołów bez pomiaru lub zatwierdzonej
decyzji. Każda faza kończy się dowodem testowym rozróżniającym build, compile/link,
 runtime, integration i HIL zgodnie z TEST_STANDARD.

## Metryki

Śledzimy regresje, rozmiar flash, heap, czas pętli, wyniki testów kontraktowych oraz
status decyzji i spike. Sama zgodność istniejącego projektu nie jest kryterium architektury.
