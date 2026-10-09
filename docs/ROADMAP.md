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
9. **FAZA 9 — Production Web + Realtime (IN PROGRESS)**: migracje produkcyjnego Web dla Luma i Hydro są zakończone; implementacja Realtime i dalszy zakres Phase 9 pozostają wymagane.
10. **FAZA 10 — Home Assistant Integration**: custom integration `aquaOne`, lokalny HTTP API client, WebSocket/Realtime client, Config Flow, Zeroconf/mDNS, Luma reference adapter, reconnect/resync i product-aware entity mapping. Plan: F10.1 architecture/client library; F10.2 Luma read-only; F10.3 Realtime reconnect/resync; F10.4 Luma commands; F10.5 Hydro adapter; F10.6 Doser MQTT replacement.
11. **FAZA 11 — OTA / Backup / Restore / Factory Reset**: wspólne workflow i recovery.
12. **FAZA 12 — UI Shell**: wspólny shell/design system po stabilizacji kontraktów. Shared Web Theme v1 jest już CURRENT foundation, ale nie zamyka całej fazy.
13. **FAZA 13 — Reference Empty Device**: minimalny klient weryfikujący platformę bez domeny.
14. **FAZA 14 — First real Domain migration**: wybór projektu dopiero po gotowej platformie.

## Obecny stan prac

W repozytorium istnieją używane moduły System, Config/Storage, Logging, Diagnostics,
Network, Web i Time. Web jest synchroniczną legacy foundation; obecny WebSocket nie istnieje.
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
pozostają TARGET/FUTURE. Realtime, Home Assistant integration, OTA i Backup/Restore również pozostają TARGET/FUTURE.

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

F9.7F1 and F9.7F2 are CLOSED. F9.7F2 implements and tests native Doser adapters,
Basic Auth and an Application-owned restart foundation without production cutover.
Doser production Web and MQTT remain LEGACY CURRENT. F9.7G is REQUIRED NEXT and
must add streaming OTA, safe maintenance ownership and physical S3 OTA HIL before
an atomic single-server cutover. Deferring OTA to Phase 11 defers the whole Doser
production Web migration; it never permits a partial two-server migration.

## Zasady bramki

Nie rozpoczynamy migracji domeny przed zakończeniem odpowiednich kontraktów platformy.
Nie wpisujemy limitów, timeoutów ani formatów protokołów bez pomiaru lub zatwierdzonej
decyzji. Każda faza kończy się dowodem testowym rozróżniającym build, compile/link,
 runtime, integration i HIL zgodnie z TEST_STANDARD.

## Metryki

Śledzimy regresje, rozmiar flash, heap, czas pętli, wyniki testów kontraktowych oraz
status decyzji i spike. Sama zgodność istniejącego projektu nie jest kryterium architektury.
