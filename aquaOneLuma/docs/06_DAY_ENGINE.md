# LumaSense — DayEngine

## 1. Odpowiedzialność

DayEngine jest modułem matematycznym. Na podstawie profilu i sekundy doby zwraca poziomy, DAY/NIGHT, indeksy etapów i postęp segmentu. Nie steruje trybem, przejściem ani hardware.

Podstawowe API:

```cpp
DayCalculation calculateSeconds(const Profile&, uint32_t secondOfDay);
```

Wersja minutowa `calculate()` pozostaje adapterem zgodności i wywołuje wersję sekundową dla początku danej minuty.

## 2. Ważność profilu

Przed użyciem profil musi przejść `ConfigValidator`:

```text
0 <= dayStartMinute < dayEndMinute < 1440
```

Fotoperiod nie może przechodzić przez północ. Wszystkie poziomy etapów i NIGHT muszą być skończone oraz należeć do 0–100%.

## 3. Etapy i pozycje

| Indeks | Etap | Pozycja |
|---:|---|---:|
| 0 | SunriseStart | 0.00 |
| 1 | Morning | 0.07 |
| 2 | Forenoon | 0.20 |
| 3 | Noon | 0.41 |
| 4 | Afternoon | 0.68 |
| 5 | Evening | 0.84 |
| 6 | Twilight | 0.93 |
| 7 | Sunset | 1.00 |

Pozycje są stałe w `Constants.h` i nie są częścią konfiguracji. Sekunda etapu jest wyznaczana jako start plus całkowita część iloczynu długości fotoperiodu i pozycji.

## 4. Granice DAY i NIGHT

```text
DAY:   dayStartSecond <= secondOfDay < dayEndSecond
NIGHT: secondOfDay < dayStartSecond albo secondOfDay >= dayEndSecond
```

Dokładny `dayStart` zwraca etap SunriseStart. Dokładny `dayEnd` należy już do NIGHT. Jeżeli `nightEnabled=false`, NIGHT zwraca osiem zer; w przeciwnym razie zwraca `nightLevels`.

Końcowa ramka Simulation jest świadomym wyjątkiem realizowanym w Core: pokazuje dokładny zapisany Sunset jako DAY przed auto-exit.

## 5. Interpolacja

DayEngine znajduje segment `currentStage → nextStage`, liczy postęp 0–1 i stosuje:

```cpp
smooth = t * t * (3 - 2 * t);
value = from + (to - from) * smooth;
```

Każdy kanał jest liczony niezależnie. Na początku segmentu wynik jest dokładnym FROM. Naturalny ruch sekunda po sekundzie zmienia cel Core i nie uruchamia co sekundę nowego transition.

## 6. DAY↔NIGHT

DayEngine jedynie zwraca zmienione `DayState`. Core uruchamia transition 5 s od bieżącego wyniku do nowych poziomów. Ta reguła ma pierwszeństwo także wtedy, gdy zmiana DAY/NIGHT wynika z wykrytego skoku czasu.

NORMAL używa `activeProfileIndex`, a SERVICE `serviceProfileIndex`; obliczenia DayEngine są dla obu identyczne.