# LumaSense — LightEngine

## 1. Odpowiedzialność

LightEngine przelicza osiem żądanych poziomów na bezpieczne logiczne poziomy wyjściowe. Nie wybiera profilu, trybu ani czasu i nie steruje GPIO. Wszystkie źródła, w tym MANUAL, CHANNEL_TEST, PREVIEW i SIMULATION, przechodzą przez ten sam silnik.

## 2. Pełny pipeline

```text
requested
→ walidacja konfiguracji i obrona przed NaN/Inf
→ channel enabled
→ globalPowerLimitPercent
→ gamma
→ calibrationMinPercent..calibrationMaxPercent
→ hardMaxPercent
→ końcowe ograniczenie 0..100
→ mapowanie na PWM
→ pwmInverted
→ hardware
```

Pierwsza walidacja całej konfiguracji odbywa się w `ConfigValidator` przed przyjęciem jej przez Core. LightEngine ma dodatkową ochronę na wypadek uszkodzonych danych runtime.

## 3. Warunki bezpiecznego zera

Dokładne 0% jest wartością OFF i omija `calibrationMinPercent`:

```text
requested <= 0 po ograniczeniu → output = 0
globalPowerLimitPercent = 0 → output = 0
enabled = false → output = 0
requested NaN/Inf → output = 0
niepoprawne parametry matematyczne kanału → output = 0
```

Dlatego dodatnie minimum kalibracji nigdy nie zapala kanału przy żądaniu zera.

## 4. Obliczenie dodatniego sygnału

Dla poprawnego i włączonego kanału:

```cpp
value = clamp(requestedPercent, 0, 100);
value *= globalPowerLimitPercent / 100;
x = value / 100;
xGamma = pow(x, gamma);
calibrated =
    calibrationMinPercent +
    xGamma * (calibrationMaxPercent - calibrationMinPercent);
limited = min(calibrated, hardMaxPercent);
output = clamp(limited, 0, 100);
```

Globalny limiter działa przed gamma. Gamma kształtuje znormalizowany sygnał. Kalibracja mapuje dodatni sygnał do fizycznego zakresu użytecznego kanału.

## 5. Semantyka hardMaxPercent

`hardMaxPercent` jest stosowane po gamma i kalibracji. Oznacza absolutne maksimum fizycznego wysterowania wyrażone w logicznych procentach.

Przykłady:

| Parametry | Wynik maksymalny |
|---|---:|
| calibration 0–100, hardMax 70 | 70% |
| calibration 20–90, hardMax 70 | 70% |
| calibration 20–90, hardMax 95 | 90% |
| calibration 20–90, hardMax 0 | 0% |
| hardMax 100 | brak dodatkowego ograniczenia |

Jeśli `hardMaxPercent < calibrationMinPercent`, hardMax wygrywa także dla najmniejszego dodatniego sygnału.

## 6. Walidacja i zabezpieczenia

LightEngine uznaje za niepoprawne:

- global limit, hardMax albo kalibrację poza 0–100 lub nieskończoną;
- minimum kalibracji większe od maksimum;
- gamma nieskończoną, NaN lub nie większą od zera;
- żądanie NaN lub Inf;
- niepoprawny wynik `pow` albo mapowania.

Każdy taki przypadek kończy się 0% dla danego kanału. Końcowy wynik jest zawsze skończony i mieści się w 0–100%.

## 7. Granica LightEngine / hardware

LightEngine zwraca procent logiczny. Hardware wykonuje:

```text
percent → duty 0..4095 → opcjonalna inwersja → GPIO
```

Dla `pwmInverted=false`: 0% → duty 0, 100% → 4095. Dla `pwmInverted=true`: 0% → duty 4095, 100% → 0. W obu przypadkach logiczne 0% oznacza fizyczne OFF.

Inwersja nie może zostać przeniesiona do LightEngine. Gamma, limity i kalibracja nie mogą zostać zdublowane w hardware.

## 8. Bieżące PWM

Obie platformy używają 1000 Hz i 12 bitów. `PWM_MAX_VALUE` wynosi 4095, a zaokrąglenie procentu odbywa się do najbliższego duty. Tylko implementacje HardwareInterface używają API LEDC.