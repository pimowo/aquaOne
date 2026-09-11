# LumaSense — system czasu

## 1. Źródło czasu

DS3231 jest podstawowym zegarem urządzenia i przechowuje UTC. Brak Wi-Fi, Internetu lub NTP nie zatrzymuje harmonogramu.

`TimeService::now()` odczytuje UTC z `RtcService` i przelicza je na czas lokalny Europe/Warsaw. `LumaCore` otrzymuje wyłącznie `LocalTime` i nie zależy od `NtpService` ani od systemowego zegara ESP32.

## 2. Walidacja DS3231

`RtcService::begin()` inicjalizuje I2C i odczytuje rejestr statusu. Stan OSF nie jest zapamiętywany tylko przy starcie: `RtcService::read()` sprawdza go ponownie przed każdym odczytem daty.

Odczyt jest nieważny przy:

- błędzie zapisu adresu rejestru, transmisji, długości odpowiedzi albo odczytu bajtu;
- ustawionym OSF;
- niepoprawnej cyfrze BCD lub niedozwolonych bitach;
- sekundzie lub minucie powyżej 59;
- godzinie poza zakresem, także w trybie 12-godzinnym;
- miesiącu poza 1–12;
- dniu poza zakresem danego miesiąca;
- błędnym 29 lutego;
- roku poza zakresem DS3231 obsługiwanym przez kod: 2000–2199.

Kalendarz uwzględnia regułę lat przestępnych dzielonych przez 4, 100 i 400. Publiczne `RtcService::isValidUtc()` udostępnia tę samą walidację modułowi NTP bez powielania zasad kalendarza.

## 3. Ustawianie UTC

`RtcService::setUtc()` zwraca `true` tylko wtedy, gdy:

1. dane wejściowe tworzą poprawną datę i czas UTC;
2. zapis siedmiu rejestrów się powiedzie;
3. OSF zostanie odczytane, wyczyszczone i ponownie potwierdzone jako wyczyszczone;
4. czas zostanie odczytany i zgadza się z zapisanym albo jest dokładnie sekundę późniejszy.

Niepoprawne argumenty są odrzucane. Błąd transmisji albo weryfikacji zapisu ustawia stan RTC jako nieważny.

## 4. Europe/Warsaw i DST

`TimeService` obsługuje Europe/Warsaw:

- CET: UTC+1;
- CEST: UTC+2;
- początek CEST: ostatnia niedziela marca o 01:00 UTC;
- koniec CEST: ostatnia niedziela października o 01:00 UTC.

`NtpService` nie wykonuje konwersji strefy ani DST. Czas odebrany z NTP pozostaje UTC aż do zapisania go w DS3231.

Pole `DeviceConfig::timezone` nie jest jeszcze podłączone do `TimeService`. UI nie może obiecywać działającej zmiany strefy przed wdrożeniem tej funkcji.

## 5. NtpService

`NtpService` jest niezależną, nieblokującą maszyną stanów. Odpowiada wyłącznie za pojedynczą próbę pobrania UTC, walidację wyniku i przekazanie go do `RtcService::setUtc()`.

Produkcyjny `EspNtpBackend` korzysta z natywnego SNTP ESP32. Backend ustawia tryb natychmiastowej synchronizacji, uruchamia maksymalnie trzy serwery, zwraca UTC przez `gmtime_r()` i zatrzymuje klienta po zakończeniu próby. Systemowy zegar ustawiany wewnętrznie przez bibliotekę SNTP nie jest źródłem czasu dla Core.

Domyślne serwery:

1. `pool.ntp.org`;
2. `time.google.com`;
3. `time.cloudflare.com`.

Konfiguracja pozwala podać od jednego do trzech niepustych serwerów. Nazwy są kopiowane do pamięci `NtpService`, więc nie zależą od czasu życia obiektu konfiguracyjnego.

### API

- `NtpService(RtcService&, NtpBackend&)` — jawnie łączy korektę z RTC i pozwala podstawić lekki backend testowy;
- `begin(nowMs)` — ustawia serwery, timeout i interwał domyślny bez uruchamiania sieci;
- `begin(config, nowMs)` — przyjmuje 1–3 serwery oraz własny timeout i interwał;
- `requestSync(wifiAvailable, nowMs)` — rozpoczyna ręczną próbę, jeśli żadna nie trwa;
- `requestPeriodicSync(wifiAvailable, nowMs)` — rozpoczyna próbę tylko po upływie interwału;
- `update(wifiAvailable, nowMs)` — sprawdza wynik backendu i timeout bez `delay()`;
- `isSyncInProgress()`, `hasSyncResult()`, `lastSyncSucceeded()` — raportują stan próby;
- `isPeriodicSyncDue(nowMs)` — informuje warstwę wyższą, czy minął interwał;
- `lastSuccessfulSyncAgeMs(nowMs, ageMs)` — zwraca wiek ostatniego sukcesu z obsługą overflow `millis()`.

`NtpService` nie konfiguruje ani nie uruchamia Wi-Fi. Warstwa wyższa przekazuje aktualną dostępność sieci przy żądaniu i w `update()`.

## 6. Timeout, retry i okresowa synchronizacja

Domyślny timeout pojedynczej próby wynosi 10 s. Upływ jest liczony przez odejmowanie `uint32_t`, dlatego działa także przy overflow `millis()`.

Po braku Wi-Fi, błędzie startu backendu, błędzie odpowiedzi, niepoprawnym UTC, timeout albo błędzie `RtcService::setUtc()` próba kończy się jako failure. NTP nie uruchamia agresywnej pętli retry. Ponowienie wymaga kolejnego `requestSync()` albo późniejszego `requestPeriodicSync()`.

Domyślny interwał synchronizacji okresowej wynosi 24 h. Jest liczony od `begin()` lub zakończenia poprzedniej próby. Sam `NtpService` nie wykonuje ciągłego odpytywania; warstwa wyższa decyduje, kiedy wywołać metodę okresową.

Kilka wywołań request podczas aktywnej próby nie uruchamia równoległych klientów SNTP.

## 7. Przepływ poprawnej korekty

1. Warstwa wyższa potwierdza dostępność Wi-Fi i wywołuje request.
2. Backend pobiera UTC.
3. `NtpService` waliduje pełną datę i czas przez `RtcService::isValidUtc()`.
4. `NtpService` wywołuje `RtcService::setUtc()`.
5. Próba ma status SUCCESS dopiero po pełnym sukcesie i weryfikacji zapisu przez RTC.
6. Kolejny `TimeService::now()` odczytuje już skorygowany DS3231.
7. Ewentualną różnicę czasu obsługuje detektor skoku z ETAPU 9.

Błąd backendu lub walidacji nie wywołuje zapisu RTC. Po błędzie `setUtc()` NtpService nie wykonuje samoczynnego ponownego zapisu.

## 8. Nieważny i odzyskany czas

Gdy `LocalTime.valid=false`, LumaCore:

- anuluje transition;
- zeruje requested i actual;
- kasuje stan inicjalizacji harmonogramu i obserwacji skoku czasu.

NTP nie blokuje startu i nie zasila Core bezpośrednio. Jeśli RTC jest nieważny, pierwszy poprawny NTP zapisuje UTC do DS3231 i czyści OSF przez `setUtc()`. Kolejny odczyt RTC staje się poprawny, a istniejący mechanizm invalid→valid uruchamia transition 60 s od 0 do celu profilu.

## 9. Nagły skok czasu

Detekcja działa tylko w NORMAL i SERVICE. Core zapamiętuje poprzednią sekundę doby i poprzednie `nowMs`, a następnie porównuje oczekiwany upływ z `millis()` z rzeczywistym upływem lokalnych sekund modulo 24 godziny.

Różnica od -3 do +3 sekund jest zwykłym jitterem. Większa różnica jest skokiem. Obliczenia obsługują północ i przepełnienie `millis()`.

Jeśli cel się zmienił:

- DAY→DAY albo NIGHT→NIGHT używa 60 s;
- DAY→NIGHT albo NIGHT→DAY używa 5 s.

W MANUAL, CHANNEL_TEST, PREVIEW, SIMULATION i OFF obserwacja jest wyłączona. Po powrocie pierwszy odczyt ustanawia nowy punkt odniesienia, więc nie tworzy fałszywego transition.

Korekta RTC przez NTP korzysta dokładnie z tego samego mechanizmu i nie wymaga osobnego przejścia w `NtpService`.

## 10. Granice bieżącej integracji

`NtpService` i produkcyjny backend są gotowe, ale nie zostały jeszcze podłączone do `main.cpp`, Storage, WWW ani pełnego zarządzania Wi-Fi. Taka integracja należy do późniejszego etapu. Testy ETAPU 12 używają deterministycznego backendu SNTP i mocka I2C; test z prawdziwą siecią i publicznym serwerem NTP pozostaje testem integracyjnym przed wydaniem.
## 11. AC3 — migracja do Aqua Core

**AC3 IMPLEMENTED.** Wspólna infrastruktura czasu znajduje się w `lib/aqua_core/include/AquaCore/Time` i `lib/aqua_core/src/Time`, w namespace `AquaCore::Time`.

Do Aqua Core przeniesiono:

- `LocalTime` i `UtcDateTime`;
- pełną walidację kalendarza UTC 2000–2199;
- dekodowanie rejestrów DS3231, kontrolę BCD i OSF przy każdym odczycie;
- zapis UTC, czyszczenie OSF oraz weryfikację readback;
- nieblokującą maszynę `NtpService`, `NtpBackend` i produkcyjny `EspNtpBackend`;
- timeout, interwał synchronizacji i kopiowanie nazw 1–3 serwerów;
- konkretną, dotychczasową konwersję `EuropeWarsawTimeService`.

### Granica I2C

`AquaCore::Time::RtcService` korzysta z neutralnego `RtcBus` oraz jawnego `RtcConfig` zawierającego piny i adres urządzenia. Nie importuje `BuildConfig.h`, globalnego `Wire` ani nazw LumaSense. Adapter w `src/time/RtcService.cpp` przekazuje globalny `Wire`, piny wybranego wariantu LumaSense i adres DS3231. Cała interpretacja rejestrów pozostaje w Aqua Core; adapter wykonuje wyłącznie operacje transportowe.

### Warstwa zgodności LumaSense

Nagłówki w `src/time` zachowują dotychczasowe nazwy:

- `LumaSense::LocalTime` i `LumaSense::UtcDateTime` są aliasami typów Aqua Core;
- `LumaSense::RtcService` jest cienkim adapterem konfiguracji I2C dziedziczącym publiczny sterownik Aqua Core;
- typy NTP są aliasami `AquaCore::Time`;
- `LumaSense::TimeService` jest cienkim wrapperem, który posiada lokalny adapter RTC i deleguje do `EuropeWarsawTimeService`.

Dzięki temu `FirmwareApp`, `LumaCore` oraz istniejące testy zachowują swoje API. Logika wykrywania skoku czasu, tolerancja ±3 s, przejścia DAY↔NIGHT i reakcja invalid→valid nadal należą do LumaSense i nie zostały przeniesione.

### Strategia strefy czasu

Aqua Core 0.2.0 udostępnia obecną implementację jako jawnie nazwaną `EuropeWarsawTimeService`. RTC i NTP pozostają wyłącznie w UTC. Nie istnieje jeszcze globalna baza stref ani dynamiczny wybór timezone. Podłączenie `DeviceConfig::timezone` oraz ogólny provider strefy pozostają TODO dla późniejszego, osobnego etapu po zebraniu wymagań innych urządzeń.