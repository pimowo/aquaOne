# LumaSense — plan testów i reprodukowalność

## 1. Status bieżący

Zestawy stage2–stage9 oraz stage11–stage12 były wcześniej przetestowane fizycznie na ESP32. W ETAPIE 13 wszystkie istniejące zestawy ponownie przeszły regresję kompilacyjną, a stage13 został uruchomiony na ESP32.

| Zestaw | Liczba przypadków | Stan |
|---|---:|---|
| test_stage2 | 16 | wcześniej PASSED; regresja kompiluje się |
| test_stage3 | 11 | wcześniej PASSED; regresja kompiluje się |
| test_stage4 | 11 | wcześniej PASSED; regresja kompiluje się |
| test_stage5 | 21 | wcześniej PASSED; regresja kompiluje się |
| test_stage6 | 21 | wcześniej PASSED; regresja kompiluje się |
| test_stage7 | 19 | wcześniej PASSED; regresja kompiluje się |
| test_stage8 | 12 | wcześniej PASSED; regresja kompiluje się |
| test_stage9 | 17 | wcześniej PASSED; regresja kompiluje się |
| test_stage11 | 21 | wcześniej PASSED; regresja kompiluje się |
| test_stage12 | 20 | wcześniej PASSED; regresja kompiluje się |
| test_stage13 | 21 | PASSED na ESP32 |
| **Razem** | **190** | **wszystkie znane przypadki PASSED** |

ETAP 10 był dokumentacyjny i nie ma katalogu `test_stage10`.

`src/main.cpp` jest produkcyjnym boot flow. Testowy scenariusz MANUAL timeout został usunięty. NTP i usługi sieciowe nie są jeszcze uruchamiane przez main.

## 2. Kontrakt zestawów

### stage2 — ModeManager i timeouty

Hierarchia trybów, returnMode, SERVICE pod override, MANUAL pod CHANNEL_TEST, timeouty, idempotencja, reset do NORMAL i overflow `millis()`.

### stage3 — TransitionEngine

Ruchomy target bez resetu czasu i FROM, wiele zmian celu, dokładne zakończenie, duration 0, overflow, przerwanie od bieżącego wyniku i integracja z harmonogramem.

### stage4 — Simulation

Faktyczne zakończenie wejścia, start 60 s, wejście podczas transition, dayStart, końcowa ramka Sunset, auto-exit, returnMode, Preview i overflow.

### stage5 — RTC i TimeService

OSF przy starcie i w runtime, BCD, zakresy i kalendarz, lata przestępne, błędy I2C, zapis UTC, czyszczenie OSF, DST i bezpieczne zero Core.

### stage6 — ConfigValidator

Indeksy, floaty, NaN/Inf, teksty, profile, TankConfig, ochrona LightEngine i odrzucenie niepoprawnej konfiguracji przez Core.

### stage7 — hardMaxPercent

Pipeline limit → gamma → kalibracja → hardMax, dokładne zero, disabled, zakresy kalibracji i skończony wynik 0–100.

### stage8 — Preview i edycja

Dowolny profil i etap, zmiany podglądu, brak zmiany profilu bazowego, fingerprint edycji, brak restartu przez naturalny ruch celu, ciągłość i priorytety.

### stage9 — skoki czasu

Jitter i próg ±3 s, skoki w przód i tył, północ, overflow, DAY↔NIGHT, brak reakcji w override, invalid→valid i dynamiczny target.

### stage11 — Storage

21 przypadków sprawdza zapis/odczyt, defaults, walidację, A/B, CRC, wersję schematu, zapisy częściowe, weryfikację po zapisie, overflow generation i pełne zachowanie `DeviceConfig`.

### stage12 — NTP i korekta RTC

20 przypadków sprawdza nieblokujący request, brak sieci, timeout, zapis UTC, walidację, błąd RTC, brak DST, invalid→valid, korekty czasu Core, brak równoległych prób, status, interwał i 1–3 serwery.

### stage13 — produkcyjny boot flow

21 przypadków sprawdza:

- start NORMAL z poprawnym hardware, RTC i configiem NVS;
- pusty Storage i defaults;
- błąd `StorageService::begin()`;
- uszkodzony rekord Storage;
- poprawność defaults;
- nieważny RTC i fizyczne/logiczne zero;
- odzyskanie RTC i transition 60 s od zera;
- reset trybu do NORMAL;
- błąd `hardware.begin()`;
- błąd zapisu PWM;
- użycie configu NVS i `pwmInverted`;
- brak modyfikacji configu podczas startu;
- brak automatycznego zapisu defaults;
- start bez NTP i Wi-Fi;
- nieblokującą iterację;
- odczyt RTC z częstotliwością 1 Hz;
- przekazanie `LocalTime` do Core;
- przekazanie `actualLevels` do hardware;
- restart po trybie specjalnym;
- runtime `isReady()==false` i bezpieczny OFF.

Test używa rzeczywistych `FirmwareApp`, `StorageService`, `TimeService`, `RtcService` i `LumaCore`, mocków Preferences/Wire oraz małego mocka `HardwareInterface`.

## 3. Kompilacja testów

PlatformIO nie jest w PATH. Kompilacja stage13 bez uploadu:

```powershell
C:\Users\piotrek\.platformio\penv\Scripts\platformio.exe test -e esp32dev -f test_stage13 --without-uploading --without-testing
```

Pełna regresja kompilacyjna wszystkich istniejących zestawów:

```powershell
C:\Users\piotrek\.platformio\penv\Scripts\platformio.exe test -e esp32dev -f test_stage2 -f test_stage3 -f test_stage4 -f test_stage5 -f test_stage6 -f test_stage7 -f test_stage8 -f test_stage9 -f test_stage11 -f test_stage12 -f test_stage13 --without-uploading --without-testing
```

Uruchomienie stage13 na urządzeniu:

```powershell
C:\Users\piotrek\.platformio\penv\Scripts\platformio.exe test -e esp32dev -f test_stage13 --upload-port COM5 --test-port COM5
```

W ETAPIE 13 wynik wyniósł `21 test cases: 21 succeeded`. Port zależy od systemu. Jeżeli płytka nie startuje automatycznie po uploadzie, należy użyć RESET/BOOT zgodnie z modułem.

## 4. Build obu platform

W `include/BuildConfig.h` należy zbudować kolejno:

```cpp
#define LUMASENSE_HARDWARE LUMASENSE_HW_LOLIN32_TEST
```

oraz:

```cpp
#define LUMASENSE_HARDWARE LUMASENSE_HW_AQMA
```

Komenda:

```powershell
C:\Users\piotrek\.platformio\penv\Scripts\platformio.exe run -e esp32dev
```

W ETAPIE 13 oba warianty zakończyły się SUCCESS. Po buildzie AQMA przywrócono `LUMASENSE_HW_LOLIN32_TEST`.

Docelowo osobne środowiska PlatformIO dla obu wariantów pozostają TODO.

## 5. Środowisko

| Element | Wersja |
|---|---|
| PlatformIO Core | 6.2.0 |
| platforma Espressif32 | 53.03.13 |
| Arduino-ESP32 | 3.1.3 |
| biblioteki Arduino ESP32 / ESP-IDF | 5.3.0+sha.489d7a2b3a |
| board | esp32dev |
| framework | arduino |
| rodzina układu | klasyczny ESP32 |

`platformio.ini` nadal zawiera nieprzypięte `platform = espressif32`. Przed wydaniem należy przypiąć dokładny odpowiednik 53.03.13.

## 6. Dalsze testy fizyczne Storage

Przed wydaniem wymagane są dodatkowo:

- zapis i odczyt przez rzeczywiste Preferences/NVS;
- restart pomiędzy save i load;
- pełne odłączenie zasilania;
- kontrolowane odcięcie zasilania podczas zapisu do nieaktywnego slotu;
- wielokrotne zapisy i kontrola zużycia partycji NVS.

## 7. Dalsze testy fizyczne czasu i NTP

Przed wydaniem należy sprawdzić:

- prawdziwe połączenie Wi-Fi z domyślnymi serwerami;
- timeout po utracie Internetu;
- korektę fizycznego DS3231 i odczyt po restarcie;
- start z OSF, późniejszy sync i transition invalid→valid;
- synchronizację 24 h;
- korektę przy granicach DST z UTC w RTC.

## 8. Dalsze testy produkcyjnego startu

Przed wydaniem należy dodatkowo sprawdzić na rzeczywistym urządzeniu:

- start z pustą i zapisaną partycją NVS;
- zaniki zasilania w różnych punktach boot flow;
- uszkodzenie lub odłączenie DS3231;
- błąd pojedynczego kanału PWM na obu platformach;
- fizyczny brak błysku dla zapisanej konfiguracji `pwmInverted`;
- brak cyklicznego spamu Serial przy `LUMASENSE_DEBUG=0`.

## 9. Pozostałe testy fizyczne i integracyjne

Pełna walidacja elektryczna AQma pozostaje TODO. Po wdrożeniu przyszłych modułów trzeba dodać testy WWW, MQTT i OTA. Storage wymaga testów migracji, factory resetu oraz importu/eksportu.

## 10. Bramka kamienia milowego

Kamień milowy jest zakończony, gdy:

1. właściwe testy Unity kompilują się;
2. testy możliwe do wykonania na ESP32 przechodzą;
3. build LOLIN32_TEST i AQMA kończy się sukcesem;
4. dokumentacja odpowiada implementacji;
5. `LUMASENSE_HARDWARE` wraca do LOLIN32_TEST;
6. funkcje niewdrożone są oznaczone jako TODO.