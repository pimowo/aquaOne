# LumaSense — TransitionEngine

## 1. Odpowiedzialność

TransitionEngine interpoluje osiem wartości `ChannelLevels` na podstawie `millis()`. Nie zna profili, trybów, RTC, LightEngine ani GPIO. Działa bez `delay()`.

## 2. Interpolacja

Dla czasu od startu:

```cpp
elapsed = nowMs - startMs;
t = clamp(elapsed / durationMs, 0, 1);
s = t * t * (3 - 2 * t);
current = from + (to - from) * s;
```

Odejmowanie `uint32_t` zachowuje poprawne działanie przez przepełnienie `millis()`. Gdy czas osiąga duration, wynik jest ustawiany dokładnie na najnowsze TO i transition przestaje być aktywny. Duration równe zero od razu ustawia TO.

## 3. Ruchomy cel

`update(nowMs, target)` podczas aktywnego przejścia:

- aktualizuje TO;
- nie zmienia pierwotnego FROM;
- nie zmienia `startMs` ani duration;
- nie rozpoczyna przejścia od nowa.

Jest to potrzebne, gdy DayEngine zmienia `requestedLevels` podczas długiego transition. Harmonogram pozostaje ruchomym celem, a po końcu nie ma przełączenia z zamrożonego celu na bieżący profil.

Duże, dyskretne zmiany, takie jak edycja profilu, wybór innego trybu lub nowy skok czasu, rozpoczynają osobny transition. Core najpierw oblicza bieżący wynik aktywnego transition i używa go jako nowego FROM, dzięki czemu nie wraca do starego początku.

## 4. Czasy używane przez Core

| Operacja | Stała | Czas |
|---|---|---:|
| pierwszy poprawny start, profil, SERVICE, MANUAL enter/exit, skok czasu bez zmiany DAY/NIGHT | `STANDARD_TRANSITION_MS` | 60 s |
| DAY↔NIGHT | `NIGHT_TRANSITION_MS` | 5 s |
| Preview enter/exit/zmiana | `PREVIEW_SMOOTH_MS` | 750 ms |
| ChannelTest enter/exit/zmiana | `PREVIEW_SMOOTH_MS` | 750 ms |
| zmiana wartości MANUAL | `PREVIEW_SMOOTH_MS` | 750 ms |
| Simulation enter/exit | `SIMULATION_ENTRY_MS` | 7,5 s |
| OFF | — | natychmiast, transition anulowany |

Jeżeli Simulation zostanie przyjęta przed pierwszą poprawną aktualizacją Core, start 60 s od zera pełni funkcję transition wejściowego. Postęp symulacji czeka na jego faktyczne zakończenie.

## 5. Integracja z Core

Po ustaleniu `requestedLevels` Core:

1. uruchamia nowe przejście tylko dla zdarzenia wymagającego nowego FROM i czasu;
2. przy naturalnym ruchu celu nie restartuje zegara;
3. podczas aktywnego przejścia wywołuje `update(nowMs, requestedLevels)`;
4. przekazuje wynik do LightEngine;
5. po zakończeniu przekazuje bieżące `requestedLevels` bez skoku.

## 6. Kontrakt API

```cpp
void start(from, to, durationMs, nowMs);
ChannelLevels update(nowMs);
ChannelLevels update(nowMs, target);
bool isActive() const;
void cancel();
```

`cancel()` tylko kończy aktywność. Core odpowiada za ustawienie bezpiecznych poziomów po anulowaniu, w szczególności dla OFF i nieważnego czasu.