# LumaSense — model danych i walidacja

## 1. Rozdział odpowiedzialności

- `DeviceConfig` jest konfiguracją, którą przyszły Storage ma utrwalać.
- `RuntimeState` opisuje bieżący stan Core i nie jest zapisywany.
- `ChannelLevels` przenosi osiem logicznych wartości procentowych.

Tryby chwilowe, timery, przejścia, poziomy MANUAL, Preview i postęp Simulation zawsze zaczynają od nowa po restarcie.

## 2. Stałe rozmiary

```text
CHANNEL_COUNT   = 8
PROFILE_COUNT   = 5
DAY_STAGE_COUNT = 8
```

`ChannelLevels::value[ch]` ma semantykę logicznego poziomu 0–100%. Fizyczne mapowanie i inwersja nie należą do tej struktury.

## 3. ChannelConfig

```cpp
struct ChannelConfig {
    bool enabled;
    char name[32];
    bool pwmInverted;
    float hardMaxPercent;
    float calibrationMinPercent;
    float calibrationMaxPercent;
    float gamma;
    uint16_t ledCount;
    float ledPowerW;
    char ledModel[32];
    char spectrumName[32];
    float colorTemperatureK;
    float wavelengthNm;
    float opticAngleDeg;
};
```

Znaczenie pól wpływających na wyjście:

- `enabled=false` wymusza logiczne 0%;
- `calibrationMinPercent..calibrationMaxPercent` jest fizycznym zakresem mapowania dodatniego sygnału po gamma;
- `hardMaxPercent` jest absolutnym maksimum wyniku po kalibracji;
- `pwmInverted` działa dopiero przy mapowaniu PWM w hardware i wymaga restartu po zmianie.

Dane opisowe oraz dane LED służą konfiguracji, generatorowi i przyszłej diagnostyce. Nie zmieniają bezpośrednio pipeline LightEngine.

## 4. Profile

Każdy `Profile` zawiera nazwę, `dayStartMinute`, `dayEndMinute`, osiem `DayStage`, ustawienia NIGHT i metadane generatora. Identyfikatory etapów:

0. SunriseStart
1. Morning
2. Forenoon
3. Noon
4. Afternoon
5. Evening
6. Twilight
7. Sunset

Godziny etapów pośrednich nie są zapisywane. DayEngine wylicza je z dwóch granic fotoperiodu i stałych pozycji.

## 5. DeviceConfig

Bieżąca struktura zawiera:

- `schemaVersion`;
- osiem konfiguracji kanałów;
- pięć profili;
- `TankConfig`;
- `activeProfileIndex` i `serviceProfileIndex`;
- `globalPowerLimitPercent`;
- bufor `timezone[48]`.

Nie zawiera jeszcze danych Wi-Fi, MQTT ani OTA. Pole `timezone` domyślnie ma wartość `Europe/Warsaw`, ale bieżący TimeService nie korzysta z niego dynamicznie i zawsze stosuje reguły Europe/Warsaw.

## 6. RuntimeState

```cpp
struct RuntimeState {
    OperatingMode mode;
    OperatingMode returnMode;
    DayState dayState;
    bool timeValid;
    uint8_t currentStageIndex;
    uint8_t nextStageIndex;
    uint16_t currentMinuteOfDay;
    ChannelLevels requestedLevels;
    ChannelLevels actualLevels;
    bool transitionActive;
};
```

`requestedLevels` jest celem wybranego trybu przed LightEngine. `actualLevels` jest wynikiem transition i całego LightEngine, nadal w logicznych procentach. Hardware dopiero później mapuje tę wartość na fizyczny duty.

## 7. Polityka ConfigValidator

Walidator:

- tylko sprawdza dane;
- nie modyfikuje i nie naprawia konfiguracji;
- zwraca `false` przy pierwszej wykrytej niezgodności;
- jest wywoływany przez `LumaCore::begin()`; błędna konfiguracja nie jest przyjmowana przez Core.

Warunki `DeviceConfig`:

- oba indeksy profilu są mniejsze od 5;
- globalny limiter jest skończony i mieści się w 0–100;
- `timezone` ma znak NUL w swoim buforze;
- wszystkie kanały, profile i `TankConfig` są poprawne.

Warunki kanału:

- bufory `name`, `ledModel`, `spectrumName` są zakończone NUL;
- `hardMaxPercent`, oba końce kalibracji są skończone i w 0–100;
- minimum kalibracji nie przekracza maksimum;
- gamma jest skończona i większa od zera;
- `ledPowerW`, `colorTemperatureK`, `wavelengthNm` są skończone i nieujemne;
- `opticAngleDeg` jest skończone i w 0–180;

Warunki profilu:

- nazwa jest zakończona NUL;
- start i koniec są mniejsze od 1440;
- start jest mniejszy od końca;
- wszystkie poziomy etapów i NIGHT są skończone i w 0–100.

Warunki akwarium: wszystkie pola `float` są skończone i nieujemne, a `intensityLevel` nie przekracza `High`.

`schemaVersion` istnieje w modelu, lecz bieżący `ConfigValidator` nie ocenia zgodności wersji. To jest odpowiedzialność przyszłego Storage przed migracją i walidacją treści.

## 8. Obrona w obliczeniach

Nawet po walidacji LightEngine ponownie sprawdza wartości wpływające na matematykę. NaN, Inf albo niepoprawna konfiguracja kanału dają bezpieczne 0% dla tego kanału. Wynik jest zawsze skończony i ograniczony do 0–100%.

## 9. Kontrakt przyszłego Storage

Przyszły Storage jest właścicielem aktywnego `DeviceConfig`. Nowe dane muszą trafić do kopii roboczej, przejść kontrolę `schemaVersion`, ewentualną migrację i `ConfigValidator`, a dopiero po udanym zapisie atomowym mogą zastąpić aktywną konfigurację. Szczegóły opisuje `08_STORAGE.md`.
