# LumaSense — maszyna stanów

## 1. Model

`ModeManager` przechowuje tryb bazowy NORMAL albo SERVICE oraz aktywne override. Bieżący `mode` jest najwyższym aktywnym priorytetem. `returnMode` zawsze wskazuje tryb, który stanie się widoczny po zakończeniu bieżącego override.

```text
OFF > CHANNEL_TEST > MANUAL > PREVIEW > SIMULATION > SERVICE > NORMAL
```

Nie jest używany ogólny stos. Cztery proste flagi przechowują MANUAL, CHANNEL_TEST, PREVIEW i SIMULATION, a `refreshModes()` wybiera najwyższą.

## 2. Przyjmowanie poleceń

Wejście do override jest przyjmowane tylko wtedy, gdy żądany tryb ma wyższy priorytet od bieżącego i nie jest już aktywny. Powtórzone albo odrzucone polecenie nie zmienia parametrów, timerów ani wartości trybu.

Wyjście z override jest przyjmowane tylko wtedy, gdy ten tryb aktualnie steruje. Przykładowo `exitManual()` wywołane podczas aktywnego CHANNEL_TEST jest odrzucone i nie kasuje timeoutu MANUAL.

Zmiana SERVICE podczas override aktualizuje tryb bazowy i `returnMode`, nie przerywając wyższego trybu. Przykład:

```text
SERVICE → MANUAL → SERVICE OFF
mode = MANUAL
returnMode = NORMAL
exit MANUAL → NORMAL
```

## 3. NORMAL i SERVICE

NORMAL korzysta z `activeProfileIndex`, a SERVICE z `serviceProfileIndex`. Oba używają DayEngine, w tym DAY, NIGHT i aktualnego czasu.

Wejście lub wyjście z SERVICE trwa 60 s. Zmiana indeksu używanego profilu oraz zmiana jego danych przy niezmienionym indeksie także uruchamia 60 s przejścia od bieżącego wyniku.

## 4. MANUAL

API Core:

```cpp
enterManual(levels, timeoutMinutes, nowMs);
setManualLevels(levels);
exitManual();
```

Dozwolone timeouty to 0, 15, 30 i 60 minut; inne wartości są traktowane jak 0. Zero oznacza brak limitu. Wejście i wyjście trwają 60 s, a zmiana poziomów podczas MANUAL 750 ms. Zmiana wartości nie resetuje timera.

MANUAL przykryty przez CHANNEL_TEST pozostaje aktywny wraz z timerem. Po zejściu CHANNEL_TEST Core ponownie ocenia timeout; wygasły MANUAL nie pozostaje bezterminowo aktywny. Obliczenia czasu używają odejmowania `uint32_t` i działają przez przepełnienie `millis()`.

## 5. CHANNEL_TEST

CHANNEL_TEST wybiera jeden kanał i zeruje pozostałe. Wejście, zmiana poziomu i wyjście używają 750 ms. Timer pięciu minut rozpoczyna się, gdy CHANNEL_TEST faktycznie steruje. Po zakończeniu wraca najwyższy nadal aktywny tryb niższego priorytetu.

## 6. PREVIEW

API Core:

```cpp
enterPreview(profileIndex, stageIndex);
setPreviewProfile(profileIndex);
setPreviewStage(stageIndex);
exitPreview();
```

Preview przechowuje osobno indeks profilu i etapu, może pokazać dowolny profil 0–4 i nie modyfikuje `activeProfileIndex`, `serviceProfileIndex` ani danych profilu. Wejście, wyjście, zmiana profilu, zmiana etapu i zmiana poziomów aktualnie podglądanego etapu używają 750 ms.

## 7. SIMULATION

`enterSimulation(durationMinutes, nowMs)` ogranicza czas symulacji do 1–15 minut. Wejście do początku dnia i wyjście do trybu powrotu używają `SIMULATION_ENTRY_MS = 7500 ms`.

Postęp nie rozpoczyna się od chwili przyjęcia polecenia. Core czeka na faktyczne zakończenie transition wejściowego. Jeśli pierwsza poprawna aktualizacja Core uruchamia jeszcze start 60 s od zera, ten start jest rzeczywistym wejściem i symulacja czeka pełne 60 s.

`progress = 0` odpowiada dokładnie `dayStart`. `progress = 1` daje osobną końcową ramkę z etapem Sunset i `DayState::Day`, mimo że zwykły DayEngine traktuje dokładny `dayEnd` jako NIGHT. Auto-exit następuje dopiero w kolejnym `update()`.

Preview może przykryć Simulation; po wyjściu Preview symulacja wraca z zachowanym postępem.

## 8. OFF i restart

OFF:

- ma najwyższy priorytet;
- anuluje wszystkie override i ich timery;
- przerywa transition;
- natychmiast ustawia logiczne poziomy na 0;
- po `exitOff()` wraca zawsze do NORMAL przez 60 s.

Powtórzone `exitOff()` poza OFF jest bezpieczne i nie resetuje startu. `ModeManager::resetToNormal()`, wywoływany przez `LumaCore::begin()`, usuwa stan wszystkich trybów. Runtime nie jest odtwarzany po restarcie.

## 9. Przejścia między trybami

| Zdarzenie | Wynik | Czas |
|---|---|---:|
| profil lub SERVICE | nowy profil bazowy | 60 s |
| MANUAL enter/exit | wyższy tryb / `returnMode` | 60 s |
| MANUAL value | ten sam MANUAL | 750 ms |
| CHANNEL_TEST enter/exit/value | wyższy tryb / `returnMode` | 750 ms |
| PREVIEW enter/exit/selection/data | wyższy tryb / `returnMode` | 750 ms |
| SIMULATION enter/exit | wyższy tryb / `returnMode` | 7,5 s |
| OFF ON | OFF | natychmiast |
| OFF OFF | NORMAL | 60 s |

Każde nowe przejście rozpoczyna się od bieżącego wyniku. Naturalny ruch harmonogramu nie restartuje aktywnego transition; aktualizuje jego cel.

## 10. Czas

Detekcja nagłego skoku działa wyłącznie w NORMAL i SERVICE. Stan obserwacji jest kasowany w trybach specjalnych i po nieważnym czasie, dlatego powrót z override nie tworzy fałszywego skoku. Przy zmianie DAY↔NIGHT pierwszeństwo ma transition 5 s; pozostały istotny skok celu używa 60 s.

## 11. Jedno źródło sterowania

Tylko bieżący `mode` dostarcza `requestedLevels`. Wszystkie tryby przechodzą przez TransitionEngine, LightEngine i HardwareInterface. Żaden tryb nie może omijać limitów, kalibracji ani zabezpieczeń wyjść.