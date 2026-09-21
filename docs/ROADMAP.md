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
2. **FAZA 2 — Logging / Storage / Config**: adaptacja istniejących mechanizmów i pełny lifecycle konfiguracji.
3. **FAZA 3 — Commands / Safety**: wspólna ścieżka komend, policy i Action Locks.
4. **FAZA 4 — Events / Alarms**: snapshot, event contracts i lifecycle alarmów.
5. **FAZA 5 — Maintenance**: wspólne workflow maintenance, restartu i przygotowania domeny.
6. **FAZA 6 — Diagnostics / Registry**: provider/capability diagnostics i jawny registry.
7. **FAZA 7 — Time / Network adaptation**: dopasowanie istniejących usług do kontraktów vNext.
8. **FAZA 8 — HTTP + WebSocket feasibility spike**: jeden backend/port, reconnect, resync, OTA i pomiary.
9. **FAZA 9 — Production Web + Realtime**: implementacja dopiero po pozytywnym spike.
10. **FAZA 10 — MQTT**: wspólna infrastruktura po ustaleniu Commands/Events.
11. **FAZA 11 — OTA / Backup / Restore / Factory Reset**: wspólne workflow i recovery.
12. **FAZA 12 — UI Shell**: wspólny shell/design system po stabilizacji kontraktów.
13. **FAZA 13 — Reference Empty Device**: minimalny klient weryfikujący platformę bez domeny.
14. **FAZA 14 — First real Domain migration**: wybór projektu dopiero po gotowej platformie.

## Obecny stan prac

W repozytorium istnieją używane moduły System, Config/Storage, Logging, Diagnostics,
Network, Web i Time. Web jest synchroniczną legacy foundation; obecny WebSocket nie istnieje.
Doser, Luma i Hydro są klientami CURRENT do późniejszej oceny, a nie wzorcem Architecture
vNext. Wspólne Commands, Events, Alarms, Safety, Maintenance, Realtime, Registry, MQTT,
OTA i Backup/Restore są jeszcze TARGET/FUTURE.

## Zasady bramki

Nie rozpoczynamy migracji domeny przed zakończeniem odpowiednich kontraktów platformy.
Nie wpisujemy limitów, timeoutów ani formatów protokołów bez pomiaru lub zatwierdzonej
decyzji. Każda faza kończy się dowodem testowym rozróżniającym build, compile/link,
 runtime, integration i HIL zgodnie z TEST_STANDARD.

## Metryki

Śledzimy regresje, rozmiar flash, heap, czas pętli, wyniki testów kontraktowych oraz
status decyzji i spike. Sama zgodność istniejącego projektu nie jest kryterium architektury.
