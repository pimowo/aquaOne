# SAFETY_STANDARD.md

**Status:** DRAFT
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard bezpieczeństwa dla całego ekosystemu **aquaOne**.

Dotyczy wszystkich urządzeń korzystających z `aquaOneCore`, m.in.:

- `aquaOneLuma`
- `aquaOneDoser`
- `aquaOneHydro`
- `aquaOneClima`
- `aquaOneGas`
- `aquaOneFauna`

Standard określa:

- ogólne zasady fail-safe,
- bezpieczny stan urządzenia,
- reakcję na błędy,
- kontrolę aktuatorów,
- blokady bezpieczeństwa,
- warunki wznowienia pracy,
- zasady startup/shutdown,
- bezpieczeństwo komend,
- ograniczenia sterowania lokalnego i zdalnego,
- watchdog i crash loop,
- zachowanie przy utracie sensorów,
- bezpieczeństwo konfiguracji,
- odpowiedzialność `aquaOneCore` i projektów domenowych.

## Zakres i brak certyfikacji

Dokument jest wewnętrznym standardem projektowym aquaOne. Nie oznacza zgodności ani
certyfikacji SIL, ASIL, IEC 61508, ISO 26262 ani podobnych norm bezpieczeństwa. Użycie pojęć
`safe state`, `fail-safe` lub `interlock` opisuje intencję projektową, nie poziom certyfikacji.

### Implementation: CURRENT

Istniejące projekty mają wybrane lokalne zabezpieczenia domenowe, timeouty i bezpieczne
ustawianie wyjść. AquaCore nie ma obecnie wspólnego `SafetyManager` ani `CommandGuard`.

### Implementation: TARGET

- **Software safety:** Core MOŻE dostarczać techniczne guardy, timeouty, watchdog i
	koordynację stanów.
- **Hardware safety:** projekt sprzętowy MUSI zapewnić właściwe poziomy boot, ograniczenia
	elektryczne i niezależne zabezpieczenia adekwatne do ryzyka.
- **Domain safety:** domena MUSI określić zagrożenia, safe state, interlocki, limity,
	recovery i manual override.

Safe state zawsze pozostaje decyzją domenową; Core nie może zgadywać bezpiecznej pozycji
pompy, zaworu, grzałki ani światła.

---

# 2. Zasada nadrzędna

Bezpieczeństwo urządzenia ma być realizowane lokalnie.

Nie może zależeć od:

- Home Assistant,
- MQTT,
- Internetu,
- zewnętrznego serwera,
- chmury,
- poprawnego działania telefonu użytkownika.

Jeśli urządzenie może wykonać niebezpieczną akcję, mechanizm zapobiegający tej akcji musi istnieć lokalnie.

---

# 3. Fail-safe by default

Jeśli system nie jest pewny, czy może bezpiecznie kontynuować pracę, powinien przejść do stanu bezpiecznego.

Domyślna zasada:

```text
unknown unsafe condition
-> stop risky action
-> preserve diagnostics
-> require safe recovery
```

Nie oznacza to automatycznego wyłączania całego urządzenia przy każdym drobnym błędzie.

Reakcja ma być proporcjonalna do ryzyka.

---

# 4. Bezpieczny stan jest domenowy

Core nie definiuje jednego uniwersalnego safe state.

Projekt domenowy określa bezpieczną reakcję dla konkretnego systemu.

Przykłady:

```text
AquaDoser -> stop pumps
Hydro -> close fill valve / stop refill pump
Clima -> disable heater / CO2 as required
Gas -> close dosing path
Fauna -> stop feeder motor
Luma -> controlled light output or off
```

---

# 5. Safe state per hazard

Bezpieczny stan powinien być definiowany względem konkretnego zagrożenia.

Nie zakładamy, że:

```text
OFF = zawsze bezpiecznie
```

Przykład: wyłączenie obiegu wody może w pewnych sytuacjach być gorsze niż pozostawienie go aktywnego.

Dlatego safe action zawsze wynika z logiki domenowej.

---

# 6. Safety action jest idempotentna

Każda akcja bezpieczeństwa musi być bezpieczna przy wielokrotnym wywołaniu.

Przykład:

```text
stopPump()
stopPump()
stopPump()
```

nie może powodować błędnego stanu.

---

# 7. Safety action ma wyższy priorytet

Akcja bezpieczeństwa ma pierwszeństwo przed:

- sterowaniem użytkownika,
- automatyką domenową,
- harmonogramem,
- MQTT command,
- WWW command,
- HA command.

---

# 8. safety_lock

Wspólny `safety_lock` jest mechanizmem blokującym działania, które nie powinny zostać wykonane w stanie zagrożenia.

Zachowanie `safety_lock` jest zgodne z `ALARM_STANDARD.md`.

---

# 9. safety_lock obejmuje wszystkie źródła sterowania

Jeśli dana akcja jest zablokowana przez safety_lock, blokada obowiązuje dla:

- WWW,
- HA,
- MQTT,
- fizycznych przycisków,
- harmonogramu,
- lokalnych automatyzacji,
- wewnętrznych callbacków.

Nie może istnieć „tylne wejście” omijające safety logic.

---

# 10. Jedna ścieżka wykonania komendy

Każda komenda powinna przechodzić przez wspólną warstwę walidacji.

Rekomendowany model:

```text
request
-> validate
-> safety check
-> state check
-> execute
-> publish actual state
```

Źródło komendy nie powinno mieć znaczenia dla mechanizmu bezpieczeństwa.

---

# 11. Brak bezpośredniego sterowania hardware z UI

WWW, MQTT i HA nie powinny bezpośrednio zmieniać GPIO.

Sterowanie zawsze przechodzi przez logikę domenową.

Nie:

```text
HTTP request -> gpio_set_level()
```

Tylko:

```text
HTTP request
-> domain command
-> safety validation
-> actuator layer
```

---

# 12. Actuator abstraction

Domena powinna sterować fizycznymi wyjściami przez warstwę aktuatorów.

Pozwala to centralnie zapewnić:

- safe defaults,
- blokady,
- timeout,
- emergency stop,
- diagnostykę.

---

# 13. Safe output at boot

Przy starcie MCU wyjścia powinny znaleźć się w bezpiecznym stanie możliwie wcześnie.

Należy uwzględnić również okres zanim aplikacja w pełni wystartuje.

---

# 14. GPIO boot state

Projekt sprzętowy powinien unikać sytuacji, w której boot-strapping lub floating GPIO może przypadkowo:

- uruchomić pompę,
- otworzyć zawór,
- włączyć grzałkę,
- uruchomić silnik.

Jeśli to możliwe, hardware ma zapewnić bezpieczny stan także przed konfiguracją GPIO przez firmware.

---

# 15. Preferowany hardware fail-safe

Dla funkcji krytycznych preferujemy sprzęt, który po utracie sterowania przechodzi w stan bezpieczny.

Przykłady:

- zawór NC zamiast NO, jeśli to właściwe dla danej funkcji,
- pull-down/pull-up,
- przekaźnik w stanie bezpiecznym po resecie,
- zewnętrzny watchdog / termik / bezpiecznik, gdy uzasadnione.

Firmware nie powinien być jedynym zabezpieczeniem, jeśli ryzyko jest wysokie.

---

# 16. Startup sequence

Rekomendowana sekwencja:

```text
set safe outputs
-> init storage
-> load config
-> validate/migrate
-> init safety/alarm manager
-> init sensors
-> init actuators
-> evaluate critical conditions
-> enable domain logic
-> ready
```

Domena nie powinna aktywować ryzykownych wyjść przed pierwszą wiarygodną oceną bezpieczeństwa.

---

# 17. Start disabled until ready

Ryzykowne funkcje powinny startować jako:

```text
disabled
```

i być włączane dopiero po spełnieniu warunków gotowości.

---

# 18. Required sensors before actuation

Jeśli dana akcja wymaga sprawnego sensora bezpieczeństwa, sensor musi być poprawny przed aktywacją aktuatora.

Przykład:

```text
heater requires valid temperature
CO2 dosing requires valid control state
refill requires valid level input
```

---

# 19. Invalid sensor blocks dependent action

Jeśli wymagany sensor staje się invalid, zależna od niego ryzykowna funkcja powinna zostać:

- zatrzymana,
- zablokowana,
- lub przełączona w jawnie zaprojektowany degraded mode.

Nie kontynuujemy sterowania na starej wartości bez jawnej decyzji domenowej.

---

# 20. Last valid value nie jest current value

Ostatni poprawny odczyt może być używany diagnostycznie, ale nie wolno go automatycznie traktować jako bieżącego stanu bezpieczeństwa.

---

# 21. Timeouts dla sensorów

Każdy sensor krytyczny powinien mieć mechanizm wykrywania braku aktualizacji.

Przykład:

```text
if no valid sample for X seconds
-> sensor invalid
-> dependent action blocked
```

Domena określa X.

---

# 22. Redundancy opcjonalna

Standard nie wymaga podwójnych sensorów.

Jeśli ryzyko uzasadnia redundancję, domena może korzystać z:

- dwóch sensorów,
- sensora + float switch,
- limit switch + pomiar analogowy,
- innego niezależnego sygnału.

Core nie narzuca architektury redundancji.

---

# 23. Cross-check sensorów

Jeśli istnieją dwa niezależne źródła informacji, domena może wykrywać niespójność.

Przykład:

```text
ultrasonic says full
float switch says low
```

Taki stan może generować ERROR/CRITICAL zależnie od znaczenia.

---

# 24. Limit czasu działania aktuatora

Ryzykowne aktuatory powinny mieć maksymalny czas ciągłej pracy, jeśli ma to sens.

Przykłady:

```text
refill pump max runtime
dosing pump max runtime
feeder motor max runtime
valve max open time
```

Przekroczenie limitu powinno zatrzymać akcję.

---

# 25. Limit dawki / ilości

Jeśli akcja ma wymiar ilościowy, domena powinna mieć rozsądny limit.

Przykład:

```text
max dose per command
max daily dose
max refill volume/time
```

Nie przyjmujemy dowolnej wartości bez walidacji.

---

# 26. Soft limit i hard limit

Jeśli ma to sens, można rozdzielić:

```text
soft limit
hard limit
```

Soft limit może wymagać potwierdzenia.

Hard limit nie może być przekroczony przez zwykłego użytkownika.

---

# 27. Safety-critical settings

Ustawienia wpływające bezpośrednio na bezpieczeństwo muszą:

- być walidowane,
- mieć rozsądne zakresy,
- być zapisywane atomowo,
- nie przyjmować przypadkowych wartości.

---

# 28. Niebezpieczne wartości domyślne są zabronione

Factory defaults nie mogą uruchamiać ryzykownego procesu tylko dlatego, że użytkownik jeszcze nic nie skonfigurował.

Preferencja:

```text
safe but inactive
```

zamiast:

```text
active with guessed parameters
```

---

# 29. Missing config

Jeśli brakuje wymaganej konfiguracji bezpieczeństwa:

- funkcja pozostaje zablokowana,
- WWW pokazuje potrzebę konfiguracji,
- status może być WARNING/ERROR,
- nie używamy zgadywanych parametrów.

---

# 30. Config validation at boot

Po każdym starcie krytyczne ustawienia są walidowane przed aktywacją funkcji domenowych.

---

# 31. Corrupted config

Jeśli krytyczna konfiguracja jest uszkodzona:

```text
do not actuate
-> recover if possible
-> safe state if not
```

Zgodnie z `CONFIG_STORAGE_STANDARD`.

---

# 32. SERVICE nie wyłącza safety

Tryb `SERVICE` nie wyłącza wspólnych zabezpieczeń.

Może świadomie:

- zmienić sposób działania,
- wyciszyć wybrane alarmy,
- umożliwić ręczne sterowanie serwisowe,

ale krytyczne interlocki pozostają aktywne, chyba że projekt posiada jawny, kontrolowany tryb testowy.

---

# 33. Brak globalnego bypass safety

Nie tworzymy jednego przycisku:

```text
Disable all safety
```

ani w WWW, ani HA.

---

# 34. Serwisowe override

Jeśli konkretny projekt potrzebuje override pojedynczej blokady, musi być:

- jawnie nazwany,
- lokalny,
- ograniczony zakresem,
- czasowy lub ręcznie wycofywany,
- widoczny w diagnostyce,
- niemożliwy do przypadkowego aktywowania.

Nie jest to funkcja Core v1 domyślnie.

---

# 35. Manual control

Sterowanie ręczne nie omija limitów bezpieczeństwa.

Przykład:

```text
manual pump ON
```

nadal podlega:

- max runtime,
- safety_lock,
- sensor interlock,
- actuator availability.

---

# 36. Manual control timeout

Jeśli ręczne włączenie aktuatora może zostać zapomniane, powinno mieć timeout.

Domena ustala sensowną wartość.

---

# 37. Idempotent commands

Komendy sterujące powinny być bezpieczne przy powtórzeniu.

Przykład:

```text
SET ON
SET ON
```

nie może tworzyć dodatkowej dawki.

Dla jednorazowych akcji używamy `PRESS` i deduplikacji tam, gdzie to potrzebne.

---

# 38. Toggle nie jest preferowany

Dla funkcji bezpieczeństwa preferujemy jawne:

```text
ON
OFF
START
STOP
```

zamiast:

```text
TOGGLE
```

Zgodnie z `MQTT_STANDARD`.

---

# 39. Command freshness

Dla akcji, które mogą być niebezpieczne po opóźnieniu, nie kolejkujemy starych komend.

MQTT offline queue nie powinna odtwarzać historycznych komend sterujących po reconnect.

---

# 40. Brak automatycznego retry ryzykownej akcji

Jeśli akcja nie została wykonana z powodu safety/state error, nie próbujemy jej automatycznie ponownie w nieskończoność.

Nowa próba wymaga nowej decyzji logiki lub użytkownika.

---

# 41. Retry tylko dla infrastruktury

Retry może dotyczyć:

- Wi-Fi,
- MQTT,
- NTP,
- odczytu niekrytycznego sensora,

ale nie powinien automatycznie powtarzać niebezpiecznej fizycznej akcji.

---

# 42. Watchdog

Urządzenia powinny korzystać z watchdogów zgodnie z platformą.

Watchdog ma wykrywać zawieszenie software, a nie zastępować poprawną architekturę.

---

# 43. Nie wyłączamy watchdog bez potrzeby

Długie operacje należy przebudować jako nieblokujące.

Nie rozwiązujemy problemu poprzez globalne wyłączenie watchdog.

---

# 44. Watchdog reset

Po watchdog reset:

- reset_reason jest widoczny,
- urządzenie startuje od safe outputs,
- zwykły mode wraca do NORMAL,
- CRITICAL latched zostają odtworzone,
- domena ponownie przechodzi pełny safety init.

---

# 45. Crash loop

Wielokrotne restarty w krótkim czasie powinny być traktowane jako problem systemowy.

Core może przejść do ograniczonego safe boot.

---

# 46. Safe boot

Safe boot może:

- uruchomić WWW,
- storage,
- diagnostykę,
- OTA,
- podstawową sieć,

ale pozostawić ryzykowne funkcje domenowe wyłączone.

---

# 47. Safe boot ≠ SERVICE

Safe boot to tryb technicznego odzyskiwania.

Nie jest normalnym SERVICE.

---

# 48. Brownout

Po brownout urządzenie nie zakłada, że poprzedni stan aktuatorów był bezpiecznie zakończony.

Po starcie zawsze przechodzi pełną inicjalizację safety.

---

# 49. Power loss

Projekt domenowy powinien przyjąć, że zasilanie może zostać odcięte w dowolnym momencie.

Nie może wymagać sekwencji shutdown do zachowania podstawowego bezpieczeństwa.

---

# 50. Controlled shutdown

Jeśli wykonywany jest kontrolowany restart/OTA:

- zatrzymujemy ryzykowne akcje,
- zapisujemy config jeśli trzeba,
- publikujemy offline jeśli możliwe,
- restartujemy.

Ale brak controlled shutdown przy nagłym zaniku zasilania nie może prowadzić do niekontrolowanego restartu procesu.

---

# 51. No resume by default

Po restarcie nie wznawiamy automatycznie przerwanej ryzykownej operacji.

Domyślna zasada:

```text
re-evaluate from current state
```

Domena może świadomie wspierać recovery tylko tam, gdzie jest to bezpieczne.

---

# 52. Harmonogram po restarcie

Harmonogram nie powinien automatycznie „odrabiać” wszystkich przegapionych akcji bez jawnej logiki domenowej.

Przykład: po 6 godzinach offline dozownik nie powinien wykonać wszystkich zaległych dawek naraz.

---

# 53. Catch-up policy jest domenowa

Każdy projekt, który ma harmonogram, powinien jawnie określić:

```text
missed event policy
```

np.:

```text
SKIP
RUN_ONCE
LIMITED_CATCHUP
```

Nie ma globalnego automatycznego catch-up.

---

# 54. Time validity

Akcje zależne od czasu nie powinny wykonywać się przy invalid time, jeśli błędny czas mógłby być niebezpieczny.

---

# 55. RTC/NTP correction

Nagła korekta czasu nie może powodować wielokrotnego wykonania tej samej akcji.

Scheduler musi być odporny na:

- cofnięcie czasu,
- przeskok do przodu,
- DST,
- NTP correction.

---

# 56. Duplicate schedule protection

Zdarzenia harmonogramu powinny posiadać logiczną ochronę przed podwójnym wykonaniem w tym samym oknie.

---

# 57. Sensor plausibility

Domena powinna w miarę możliwości sprawdzać nie tylko zakres techniczny sensora, ale również plausibility.

Przykład:

```text
temperature jump +40°C in 1 second
```

może wskazywać błąd.

---

# 58. Out-of-range input

Wejście poza fizycznie sensownym zakresem powinno być traktowane jako invalid/fault, a nie jako normalny odczyt.

---

# 59. Stuck sensor

Jeśli dla krytycznego sensora istnieje możliwość wykrycia wartości „zamrożonej”, domena może implementować stuck detection.

Nie jest to wymagane dla każdego sensora.

---

# 60. Actuator feedback

Jeśli sprzęt posiada feedback aktuatora, należy go wykorzystać.

Przykłady:

- kontaktron,
- current sense,
- pressure response,
- limit switch.

Brak feedbacku nie jest automatycznie błędem, jeśli sprzęt go nie posiada.

---

# 61. Commanded state ≠ confirmed state

Jeśli istnieje feedback, rozróżniamy:

```text
commanded state
confirmed physical state
```

Nie zakładamy, że przekaźnik zadziałał tylko dlatego, że GPIO zostało ustawione.

---

# 62. Actuator timeout fault

Jeśli aktuator powinien osiągnąć potwierdzony stan w określonym czasie i tego nie zrobi:

```text
fault
-> alarm if appropriate
-> safe reaction
```

---

# 63. Interlocks

Domena może definiować interlocki, np.:

```text
heater only if flow present
CO2 only if circulation active
refill only if reservoir available
dose only if pump calibrated
```

Docelowo Core MOŻE dostarczać mechanizm walidacji komendy, ale nie zna domenowej semantyki.

---

# 64. Mutual exclusion

Funkcje, które nie mogą działać jednocześnie, powinny mieć jawny interlock.

Przykład:

```text
FILL and DRAIN cannot run together
```

Nie polegamy wyłącznie na UI, że użytkownik nie włączy obu.

---

# 65. Priority of stop

Komenda zatrzymania bezpiecznej funkcji powinna mieć możliwość wykonania nawet wtedy, gdy normalne komendy są blokowane.

Przykład:

```text
STOP pump
```

powinno działać przy safety_lock, jeśli faktycznie zwiększa bezpieczeństwo.

---

# 66. Emergency stop semantics

Jeśli projekt posiada emergency stop, powinien być:

- lokalny,
- najwyższego priorytetu,
- niezależny od HA/MQTT,
- idempotentny.

Nie każdy projekt musi mieć fizyczny E-STOP.

---

# 67. No hidden re-enable

Po zadziałaniu safety nie może istnieć ukryta ścieżka automatycznego ponownego uruchomienia procesu.

Wznowienie następuje zgodnie z warunkami domenowymi i `ALARM_STANDARD`.

---

# 68. CRITICAL recovery

Po `CRITICAL`:

```text
cause gone
+ safe resume condition met
+ ACK
-> allow recovery
```

Nigdy samo:

```text
cause gone -> resume
```

---

# 69. Multiple critical causes

Jeśli istnieje kilka blokad CRITICAL, wszystkie muszą zostać rozwiązane przed zdjęciem safety_lock.

---

# 70. Safety condition aggregation

Core może agregować blokady, ale domena definiuje ich znaczenie.

Nie upraszczamy kilku niezależnych zagrożeń do jednej niejasnej flagi bez możliwości diagnostyki.

---

# 71. Logging safety events

Ważne zdarzenia bezpieczeństwa są logowane:

```text
safe state entered
safety_lock set
command blocked
safe resume allowed
safety_lock cleared
critical interlock failed
```

---

# 72. Brak flood logów

Ten sam trwający warunek nie powinien generować logu w każdej iteracji loop.

Logujemy zmiany stanu i istotne zdarzenia.

---

# 73. Safety diagnostics

WWW powinno pokazywać:

- safety_lock,
- aktywne CRITICAL,
- blokujące interlocki,
- disabled actuators,
- reason codes.

Nie musi pokazywać wszystkich wewnętrznych flag.

---

# 74. Safety a HA

HA może widzieć:

```text
safety_lock
alarm_active
alarm_severity
```

Nie musi dostawać pełnej macierzy interlocków.

Pełna diagnostyka pozostaje w WWW.

---

# 75. Local-first recovery

Naprawa po problemie powinna być możliwa lokalnie, bez HA.

WWW i fizyczne sterowanie serwisowe mają działać niezależnie od MQTT.

---

# 76. Network loss during action

Utrata Wi-Fi/MQTT podczas wykonywania lokalnej akcji nie może sama powodować niebezpiecznego stanu.

Urządzenie kontynuuje lub bezpiecznie zatrzymuje akcję zgodnie z logiką domenową, nie z dostępnością sieci.

---

# 77. Broker reconnect nie odtwarza komend

Po reconnect urządzenie publikuje stan i snapshot.

Nie wykonuje ponownie dawnych komend.

---

# 78. HA restart nie wpływa na safety

Restart HA nie może zmieniać lokalnego safe state, alarmów ani safety_lock.

---

# 79. Błąd WWW nie wpływa na domenę

Awaria WebService nie może zatrzymywać głównej logiki bezpieczeństwa.

---

# 80. Błąd loggera nie może blokować safety

Logger jest pomocniczy.

Jeśli logger nie może zapisać wpisu, safety action nadal musi zostać wykonana.

---

# 81. Błąd MQTT nie może blokować safety

Publikacja alarmu może się nie udać.

Safe state i safety_lock nadal muszą działać lokalnie.

---

# 82. Błąd storage podczas safety

Jeśli trwały zapis latcha się nie powiedzie, Core loguje storage error.

Jeśli utrata latcha po restarcie mogłaby stworzyć ryzyko, problem storage może eskalować do CRITICAL.

---

# 83. Buzzer nie jest zabezpieczeniem

Buzzer jest tylko sygnalizacją.

Wyłączenie buzzera nie wpływa na:

- alarm,
- safe state,
- safety_lock,
- interlocki.

---

# 84. User feedback

Jeśli użytkownik próbuje wykonać zablokowaną akcję, UI powinno pokazać powód.

Przykład:

```text
Action blocked: SAFETY_LOCK
```

Nie wystarczy ignorować kliknięcia.

---

# 85. Stable reason codes

Powody blokad powinny używać stabilnych kodów technicznych, np.:

```text
SAFETY_LOCK
SENSOR_INVALID
ACTUATOR_FAULT
CONFIG_INVALID
MAX_RUNTIME
NOT_READY
INTERLOCK_ACTIVE
```

---

# 86. Safety state machine

Dla bardziej złożonych procesów preferowana jest jawna maszyna stanów zamiast wielu rozproszonych booli.

Przykład:

```text
IDLE
PREPARE
RUNNING
STOPPING
FAULT
LOCKED
```

Nie każdy projekt musi mieć identyczne stany.

---

# 87. Illegal transition protection

Maszyna stanów powinna blokować niedozwolone przejścia.

Nie wykonujemy przypadkowego:

```text
FAULT -> RUNNING
```

bez poprawnego recovery.

---

# 88. Command/state validation

Przed wykonaniem komendy sprawdzamy, czy jest legalna w bieżącym stanie.

Przykład:

```text
START only from IDLE
ACK only if alarm exists
CALIBRATE only in safe condition
```

---

# 89. Critical code simplicity

Ścieżki safety powinny być możliwie proste.

Unikamy zbędnej dynamicznej alokacji, ciężkich callback chainów i skomplikowanych zależności w kodzie krytycznym.

---

# 90. Deterministyczność

Reakcja bezpieczeństwa powinna być deterministyczna.

Ten sam stan wejściowy i konfiguracja powinny prowadzić do tej samej decyzji.

---

# 91. Brak `delay()` w safety logic

Mechanizmy safety mają być nieblokujące.

Używamy:

- timestampów,
- state machine,
- timerów.

---

# 92. Safety task priority

Jeśli architektura używa FreeRTOS tasks, logika bezpieczeństwa nie powinna być uzależniona od taska o niskim priorytecie, który łatwo zagłodzić.

Dokładne priorytety pozostają implementacyjne.

---

# 93. Shared resource protection

Jeśli kilka modułów steruje wspólnym zasobem, dostęp musi być kontrolowany.

Nie może być sytuacji, że dwa niezależne moduły jednocześnie ustalają stan tego samego aktuatora bez arbitrażu.

---

# 94. Ownership aktuatora

Każdy fizyczny aktuator powinien mieć jedno logiczne miejsce odpowiedzialne za finalną decyzję o jego stanie.

---

# 95. Hardware revision safety

Jeśli zachowanie zależy od rewizji hardware, firmware musi wiedzieć, z jaką rewizją pracuje lub mieć konfigurację pozwalającą uniknąć błędnego sterowania.

---

# 96. Wrong firmware protection

OTA oraz boot validation powinny ograniczać ryzyko uruchomienia firmware dla innego typu urządzenia / innego hardware.

Zgodnie z `OTA_STANDARD`.

---

# 97. Safety self-test

Przy boot można wykonywać lekki self-test funkcji safety.

Przykłady:

- config valid,
- required sensor present,
- actuator driver initialized,
- safety manager initialized.

Nie wykonujemy automatycznie niebezpiecznych ruchów testowych bez potrzeby.

---

# 98. Testowanie hardware output

Jeśli test fizycznego aktuatora wymaga ruchu/załączenia, powinien być świadomą akcją serwisową, nie automatycznym boot testem.

---

# 99. Testy safety

Każdy projekt powinien mieć testy co najmniej dla:

- invalid sensor,
- sensor timeout,
- safety_lock,
- blocked command,
- manual control timeout,
- max runtime,
- restart during action,
- power-loss recovery,
- config corruption,
- multiple CRITICAL,
- ACK recovery,
- watchdog restart,
- OTA during safe state.

---

# 100. Fault injection

Warto wspierać development/test mode pozwalający symulować:

```text
sensor failure
storage failure
MQTT failure
critical alarm
actuator fault
```

Nie powinien być aktywny przypadkowo w produkcji.

---

# 101. Safety test mode

Jeśli powstanie test mode, powinien być:

- development/service only,
- wyraźnie oznaczony,
- wyłączony domyślnie,
- lokalny,
- łatwy do rozpoznania diagnostycznie.

---

# 102. Core API — odpowiedzialność

`aquaOneCore` powinien docelowo dostarczyć mechanizmy odpowiadające za:

```text
SafetyManager
SafetyLock
CommandGuard
InterlockRegistry
ActuatorGuard
SafeStateCoordinator
BootSafetyCoordinator
```

Nazwy klas mogą się różnić, ale podział odpowiedzialności powinien pozostać.

---

# 103. SafetyManager

Powinien odpowiadać za:

- globalny safety_lock,
- rejestrację blokad,
- agregację powodów,
- wspólne sprawdzanie komend,
- koordynację safe actions,
- recovery gate.

Nie powinien znać szczegółowej semantyki procesu.

---

# 104. CommandGuard

Powinien dostarczać wspólny mechanizm:

```text
canExecute(command)
```

z wynikiem:

```text
allowed
reason_code
```

Dzięki temu WWW, MQTT, HA i lokalne sterowanie zachowują się identycznie.

---

# 105. InterlockRegistry

Domena może rejestrować warunki blokujące akcje.

Przykład logiczny:

```text
registerInterlock("heater", ...)
registerInterlock("dose", ...)
```

Nie jest wymagane dokładnie takie API.

---

# 106. Domena — odpowiedzialność

Projekt domenowy odpowiada za:

- definicję hazardów,
- safe state,
- required sensors,
- limity czasu,
- limity ilości,
- interlocki,
- recovery conditions,
- actuator ownership,
- scheduler safety,
- manual control safety.

---

# 107. Core nie zgaduje bezpieczeństwa domeny

Core nie powinien sam decydować, że np.:

```text
OFF = safe
pump should stop
heater should stop
```

bez informacji od domeny.

---

# 108. Integracja z ALARM_STANDARD

CRITICAL:

- aktywuje safe state,
- ustawia safety_lock,
- jest latched,
- wymaga ACK do recovery.

Safety logic i alarmy są powiązane, ale nie są tym samym modułem.

---

# 109. Integracja z CONFIG_STORAGE_STANDARD

Safety-critical config:

- jest walidowany,
- zapisywany atomowo,
- ma bezpieczne defaulty,
- może korzystać z last_known_good.

---

# 110. Integracja z DIAGNOSTICS_STANDARD

Safety udostępnia diagnostycznie:

```text
safety_lock
active interlocks
blocked command reason
safe state reason
```

Bez nadmiernego rozbudowywania HA.

---

# 111. Integracja z OTA_STANDARD

Podczas OTA domena przechodzi do bezpiecznego stanu.

OTA nie omija safety_lock.

---

# 112. Integracja z WEB_STANDARD

WWW:

- nie omija safety,
- pokazuje reason code,
- blokuje nielegalne akcje,
- umożliwia ACK i diagnostykę,
- nie oferuje globalnego „disable safety”.

---

# 113. Integracja z MQTT_STANDARD

MQTT commands:

- nie są kolejkowane do późniejszego ryzykownego wykonania,
- przechodzą CommandGuard,
- publikują tylko rzeczywisty wynik,
- są odrzucane jawnie przy safety_lock.

---

# 114. Reguła końcowa

System bezpieczeństwa ma być lokalny, prosty i przewidywalny.

**Core MOŻE docelowo zapewnić mechanizmy blokad, koordynację i guardy. Domena definiuje, co
w konkretnym urządzeniu jest niebezpieczne, jaki jest safe state i jak bezpiecznie reagować.**
