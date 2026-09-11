# LumaSense — specyfikacja główna

## 1. Cel i stan projektu

LumaSense jest autonomicznym sterownikiem oświetlenia LED do akwarium. Harmonogram, profile, zegar RTC, przejścia, konfiguracja NVS i zabezpieczenia wyjść działają lokalnie. Brak Wi-Fi, WWW, MQTT, NTP lub OTA nie zatrzymuje podstawowej pracy lampy.

ETAPY 1–13 ustaliły i przetestowały kontrakty Core, hardware, trybów, przejść, symulacji, czasu, walidacji, LightEngine, Preview, korekt czasu, Storage, NTP oraz produkcyjnego startu. `src/main.cpp` jest normalnym firmware startowym; nie zawiera już scenariusza testowego MANUAL.

## 2. Platformy sprzętowe

Kod obsługuje dwa cele wybierane przez `LUMASENSE_HARDWARE`:

- `LUMASENSE_HW_LOLIN32_TEST` → `Lolin32Hardware`;
- `LUMASENSE_HW_AQMA` → `AqmaHardware`.

Obie implementacje udostępniają osiem logicznych kanałów CH1–CH8. DS3231 jest podstawowym źródłem czasu i przechowuje UTC.

## 3. Architektura sterowania

Każde źródło światła korzysta z jednego toru:

```text
profil lub tryb
→ FirmwareApp
→ LumaCore / ModeManager
→ TransitionEngine
→ LightEngine
→ HardwareInterface
→ PWM i inwersja
→ GPIO
```

WWW, MQTT, generator profili ani inne przyszłe integracje nie mogą sterować PWM bezpośrednio.

`FirmwareApp` odpowiada za wybór konfiguracji, uruchomienie usług, cykliczny odczyt czasu, wywołanie Core i przekazanie `actualLevels` do hardware. Nie implementuje Wi-Fi ani usług sieciowych.

## 4. Model dnia i profile

Urządzenie przechowuje pięć profili. Każdy profil zawiera start i koniec dnia, osiem zestawów poziomów oraz opcjonalne stałe poziomy nocne. Pozycje etapów w fotoperiodzie są stałe: `0.00`, `0.07`, `0.20`, `0.41`, `0.68`, `0.84`, `0.93`, `1.00`.

Konfiguracja wymaga `dayStartMinute < dayEndMinute`; profil nie może przechodzić przez północ. W zwykłym obliczeniu DayEngine dokładny `dayEndMinute` należy już do NIGHT. Pomiędzy etapami używany jest `smoothstep`.

## 5. Tryby i priorytety

Priorytet źródeł sterowania:

```text
OFF > CHANNEL_TEST > MANUAL > PREVIEW > SIMULATION > SERVICE > NORMAL
```

Wyższy tryb może przykryć niższy, a `returnMode` pokazuje rzeczywisty tryb powrotu. OFF anuluje override, natychmiast zeruje wyjścia i wraca do NORMAL. Każdy restart i każde `LumaCore::begin()` zaczyna w NORMAL. Tryby runtime nie są zapisywane ani odtwarzane przez Storage.

## 6. Czasy przejść

| Zdarzenie | Czas |
|---|---:|
| start po uzyskaniu poprawnego czasu | 60 s |
| zmiana profilu, SERVICE, MANUAL enter/exit | 60 s |
| nagły skok czasu DAY→DAY lub NIGHT→NIGHT | 60 s |
| DAY↔NIGHT, także po skoku czasu | 5 s |
| Preview enter/exit i zmiana podglądu | 750 ms |
| ChannelTest enter/exit i zmiana wartości | 750 ms |
| zmiana wartości MANUAL | 750 ms |
| Simulation enter/exit | 7,5 s, z wyjątkiem aktywnego startu 60 s |
| wejście do OFF | natychmiast |
| wyjście z OFF | 60 s od zera |

Aktywny transition może otrzymywać ruchomy cel bez resetu czasu i bez zmiany pierwotnego FROM. Nowe przejście używa bieżącego wyniku poprzedniego jako FROM.

## 7. Semantyka światła

Tor jednego kanału:

```text
requested
→ walidacja
→ enabled
→ globalPowerLimitPercent
→ gamma
→ calibrationMinPercent..calibrationMaxPercent
→ hardMaxPercent
→ ograniczenie 0..100
→ mapowanie PWM
→ pwmInverted
→ hardware
```

`requested = 0` zawsze oznacza fizyczne OFF, również przy dodatnim `calibrationMinPercent` i odwróconym PWM. `hardMaxPercent` jest absolutnym maksimum fizycznego wysterowania po gamma i kalibracji.

## 8. Czas i NTP

RTC przechowuje UTC. `TimeService` przelicza UTC według Europe/Warsaw: CET UTC+1 i CEST UTC+2, ze zmianą o 01:00 UTC w ostatnią niedzielę marca i października.

OSF DS3231 jest sprawdzane przy każdym odczycie. Błąd I2C, OSF albo niepoprawna data oznaczają nieważny czas. Core utrzymuje wtedy wszystkie kanały na 0%. Po odzyskaniu czasu zaczyna przejście 60 s od zera.

NTP jest opcjonalnym źródłem korekty RTC. `NtpService` pobiera UTC i zapisuje je przez `RtcService::setUtc()`. Core nie zależy od NTP. Produkcyjny boot flow ETAPU 13 nie uruchamia synchronizacji, ponieważ nie ma jeszcze zarządzania Wi-Fi.

## 9. Konfiguracja i Storage

`ConfigValidator` sprawdza pełny `DeviceConfig`. Nie naprawia danych i zwraca `false` przy błędzie. LightEngine ma dodatkowe zabezpieczenia przed NaN i Inf.

`StorageService` używa Preferences/NVS oraz slotów A/B z CRC i generacją. Przy poprawnym rekordzie `FirmwareApp` używa konfiguracji NVS. Pusty, uszkodzony lub niedostępny Storage powoduje użycie `createDefaultConfig()` w RAM. Defaults nie są automatycznie zapisywane.

`pwmInverted` jest ustawieniem sprzętowym kopiowanym podczas `hardware.begin()`. Zmiana wymaga restartu.

## 10. Produkcyjny boot flow

Po restarcie `FirmwareApp`:

1. tworzy bezpieczne defaults;
2. otwiera Storage i próbuje załadować pełny, zweryfikowany config;
3. pozostawia defaults przy każdym błędzie Storage/load;
4. ponownie waliduje aktywny config;
5. wywołuje jedyne `hardware.begin(config.channels)` z właściwym `pwmInverted`;
6. uruchamia `TimeService` i RTC;
7. uruchamia `LumaCore` w NORMAL;
8. w nieblokującym loop odświeża RTC raz na sekundę, wykonuje `core.update()` i wysyła `actualLevels` do hardware.

Rozstrzygnięcie configu poprzedza inicjalizację PWM, ponieważ bez `pwmInverted` nie istnieje jeden fizyczny poziom OFF poprawny dla wszystkich konfiguracji. Do wywołania `hardware.begin()` piny pozostają w stanie resetowym; samo `begin()` ustawia poprawny OFF przed podłączeniem LEDC.

## 11. Fail-safe

Błąd `hardware.begin()` blokuje start Core i normalną pętlę sterowania oraz wywołuje `allChannelsOff()`.

Jeżeli podczas pracy `isReady()` stanie się false albo dowolny `setChannelPercent()`/`allChannelsOff()` zwróci false, `FirmwareApp`:

- oznacza hardware jako FAILED;
- ponownie wywołuje `allChannelsOff()`;
- zatrzymuje normalne sterowanie PWM;
- pozostaje dostępny dla zwięzłej diagnostyki Serial.

Błąd Storage nie blokuje lampy. Błąd RTC nie blokuje Core, lecz utrzymuje wyjścia na 0 do odzyskania poprawnego czasu.

## 12. Integracje pozostające na później

Nie są jeszcze podłączone do produkcyjnego boot flow:

- konfiguracja Wi-Fi i WWW;
- MQTT i Home Assistant;
- realne wywołanie NTP po uzyskaniu Wi-Fi;
- OTA i rollback;
- import, eksport, factory reset i migracje konfiguracji;
- generator profili.

## 13. Kryterium wydania

Każdy kamień milowy wymaga:

- kompilacji wariantów LOLIN32_TEST i AQMA;
- kompilacji właściwych zestawów Unity;
- testu fizycznego funkcji zależnych od GPIO, PWM, I2C lub czasu;
- przywrócenia `LUMASENSE_HARDWARE` do `LUMASENSE_HW_LOLIN32_TEST` po kontroli AQMA.

Szczegóły i aktualny stan znajdują się w `12_TEST_PLAN.md`.