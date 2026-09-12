# TEST_STANDARD.md

**Status:** DRAFT
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard testowania dla całego ekosystemu **aquaOne**.

Dotyczy:

- `aquaOneCore`,
- wszystkich projektów domenowych,
- wspólnych usług,
- logiki urządzeń,
- migracji,
- OTA,
- MQTT,
- WWW,
- alarmów,
- bezpieczeństwa,
- hardware-in-the-loop tam, gdzie jest potrzebny.

Celem jest zapewnienie, że kolejne zmiany nie psują działających projektów i że wspólny Core pozostaje stabilny.

## Stan implementacji

### Implementation: CURRENT

Repozytorium ma buildy PlatformIO, wybrane testy Core i projektów oraz ręczne scenariusze
sprzętowe. Zakres i sposób wykonania różnią się między projektami. Samo zbudowanie test binary
nie jest dowodem wykonania testów runtime.

### Implementation: REQUIRED NEXT

Każda zmiana MUSI raportować rodzaj dowodu, komendę, environment, target, commit, liczbę
wykonanych testów i wynik. Zmiana wymagająca sprzętu MUSI oddzielać compile/link od HIL.

### Implementation: TARGET

Spójne fixture'y, test doubles, automatyzacja HIL, soak, power-loss i release matrix są
rozwijane etapowo. Brak takiej infrastruktury nie może być przedstawiany jako wykonany test.

---

# 2. Zasada nadrzędna

Testy mają chronić przed regresją.

Nie testujemy dla samego procentu coverage.

Najważniejsze są:

- krytyczne ścieżki,
- safety,
- storage,
- migracje,
- komunikacja,
- restart/recovery,
- błędy wejścia,
- zachowanie bez HA/MQTT/Internetu.

---

# 3. Poziomy testów

W aquaOne wyróżniamy:

```text
UNIT
COMPONENT
INTEGRATION
HIL
SYSTEM
REGRESSION
```

---

# 4. UNIT

Testuje małą jednostkę logiki bez hardware.

Przykłady:

- parser,
- walidator,
- alarm delay/hysteresis,
- scheduler logic,
- config migration,
- command guard.

---

# 5. COMPONENT

Testuje cały moduł Core lub domeny z mockowanymi zależnościami.

Przykłady:

- AlarmManager,
- ConfigManager,
- MqttService,
- SafetyManager,
- TimeManager.

---

# 6. INTEGRATION

Testuje współpracę kilku modułów.

Przykład:

```text
ConfigManager + Storage + Migration
AlarmManager + SafetyManager
MQTT + HA Discovery + State Registry
Web API + ConfigManager
```

---

# 7. HIL

Hardware-in-the-loop testuje rzeczywisty sprzęt.

Przykłady:

- GPIO,
- przekaźniki,
- sensory,
- RTC,
- pamięć,
- reboot,
- OTA,
- Wi-Fi,
- watchdog,
- prawdziwy broker MQTT.

---

# 8. SYSTEM

Testuje urządzenie jako całość.

Przykład:

```text
power on
-> boot
-> load config
-> sensors
-> domain logic
-> MQTT
-> WWW
-> alarms
-> recovery
```

---

# 9. REGRESSION

Regresja oznacza powtarzanie najważniejszych scenariuszy po zmianach Core lub domeny.

Każdy release powinien przejść minimalny zestaw regresyjny.

---

# 10. Test pyramid

Preferowana kolejność:

```text
dużo szybkich UNIT/COMPONENT
mniej INTEGRATION
wybrane HIL/SYSTEM
```

Nie próbujemy testować wszystkiego wyłącznie na fizycznym ESP.

---

# 11. Testy Core bez hardware

`aquaOneCore` powinien być projektowany tak, aby większość logiki dało się testować na PC.

To wymaga izolacji:

- hardware adapters,
- storage backend,
- time source,
- MQTT transport,
- GPIO.

---

# 12. Dependency injection

Tam, gdzie poprawia testowalność, preferujemy przekazywanie interfejsów/adapterów zamiast globalnych singletonów.

Nie komplikujemy architektury ponad potrzebę.

---

# 13. Mocki

Mockujemy tylko granice systemu.

Przykłady:

```text
clock
storage
mqtt transport
sensor
actuator
network
```

Nie mockujemy bez potrzeby samej logiki, którą chcemy przetestować.

---

# 14. Deterministyczny czas

Logika zależna od czasu powinna móc korzystać z testowego `Clock`.

Dzięki temu testy:

- delay_on,
- delay_off,
- timeout,
- scheduler,
- backoff,

nie wymagają realnego czekania.

---

# 15. Brak sleep w unit tests

Testy unit nie powinny czekać realnych sekund.

Symulujemy upływ czasu.

---

# 16. Alarm tests

`AlarmManager` powinien mieć testy co najmniej dla:

- aktywacji,
- clear,
- ACK,
- ACK ALL,
- latched,
- CRITICAL auto-latched,
- delay_on,
- delay_off,
- hysteresis,
- suppression SERVICE,
- startup grace,
- priority,
- active_alarm selection,
- active_alarm_count,
- alarm_severity,
- buzzer gating,
- safety_lock.

---

# 17. Alarm restart tests

Testujemy:

- ordinary alarm ACK not persisted,
- latched persisted,
- latched returns unacknowledged after restart,
- CRITICAL remains blocking until correct recovery.

---

# 18. Safety tests

Każdy projekt powinien testować:

- blocked command,
- invalid sensor,
- required sensor missing,
- max runtime,
- manual timeout,
- interlock,
- multiple CRITICAL,
- recovery condition,
- no hidden resume,
- all command sources use same guard.

---

# 19. Config tests

`ConfigManager` powinien mieć testy:

- defaults,
- valid load,
- invalid load,
- validation,
- normalize,
- no-change save,
- atomic save,
- interrupted save,
- last known good,
- partial corruption,
- factory reset.

---

# 20. Migration tests

Każda nowa migracja musi mieć test:

```text
old schema input
-> migrate
-> validate
-> expected current schema
```

---

# 21. Migration chain

Testujemy również migrację przez kilka wersji:

```text
v1 -> v2 -> v3 -> current
```

---

# 22. Invalid future schema

Firmware powinien poprawnie obsłużyć config schema nowszy niż obsługiwany.

Nie może go interpretować losowo.

---

# 23. MQTT tests

Wspólny MQTT powinien mieć testy:

- connect,
- auth failure,
- reconnect,
- backoff,
- keepalive,
- LWT,
- availability,
- full snapshot,
- command parsing,
- duplicate command safety,
- retained state,
- non-retained event,
- Discovery,
- Discovery cleanup,
- invalid payload,
- QoS1/PUBACK flow,
- reconnect sync gate.

---

# 24. MQTT offline tests

Testujemy:

- device continues locally,
- no offline command replay,
- events not buffered,
- snapshot sent after reconnect,
- state becomes consistent.

---

# 25. MQTT topic contract tests

Test może automatycznie sprawdzać, że topic registry nie tworzy topiców poza dozwolonymi gałęziami:

```text
availability
state
command
event
```

---

# 26. HA Discovery tests

Sprawdzamy:

- stable unique_id,
- correct device block,
- shared availability,
- entity category,
- no duplicate entities,
- stale Discovery cleanup.

---

# 27. Web API tests

Testujemy:

- routing,
- validation,
- success response,
- error response,
- HTTP status codes,
- secret masking,
- blocked actions,
- config save/apply,
- factory reset protection.

---

# 28. Web frontend smoke test

Nie wymagamy ciężkiego browser automation dla każdej zmiany.

Minimalnie sprawdzamy, że:

- strony się ładują,
- podstawowe API odpowiada,
- formularze działają,
- nie ma zależności od CDN.

---

# 29. OTA tests

Testujemy:

- valid firmware,
- invalid image,
- wrong device type,
- wrong hardware revision,
- insufficient space,
- interrupted upload,
- reboot,
- pending validation,
- mark valid,
- rollback,
- migration failure,
- no Internet after update.

---

# 30. OTA power-loss tests

Na realnym hardware warto testować odcięcie zasilania:

- podczas upload,
- po write,
- przed reboot,
- podczas first boot.

---

# 31. Diagnostics tests

Testujemy:

- status aggregation,
- module health,
- reset reason mapping,
- ring buffer,
- error codes,
- no secret leak,
- runtime counters,
- RECOVERED storage state.

---

# 32. Onboarding tests

Testujemy:

- fresh device,
- AP start,
- Wi-Fi save,
- wrong password,
- fallback AP,
- reconnect,
- network reset,
- factory reset,
- recovery access.

---

# 33. Time tests

Jeśli projekt używa czasu, testujemy:

- RTC valid,
- RTC invalid,
- NTP sync,
- correction,
- no network,
- DST/time jump,
- backward jump,
- scheduler duplicate protection.

---

# 34. Scheduler tests

Każdy scheduler domenowy powinien mieć scenariusze:

```text
normal execution
reboot before event
reboot after event
missed event
time jump forward
time jump backward
DST
invalid time
```

---

# 35. Missed-event policy tests

Jeśli domena używa:

```text
SKIP
RUN_ONCE
LIMITED_CATCHUP
```

każda polityka musi mieć jawne testy.

---

# 36. Sensor tests

Testujemy co najmniej:

- valid value,
- invalid value,
- timeout,
- disconnected,
- out-of-range,
- recovery,
- stale value handling.

---

# 37. Actuator tests

Testujemy:

- OFF default,
- command execute,
- blocked command,
- timeout,
- repeated same command,
- emergency stop,
- restart during active state.

---

# 38. Hardware boot tests

Na realnym urządzeniu sprawdzamy fizyczny stan wyjść:

```text
power on
reset
brownout/restart
bootloader
application startup
```

Ryzykowny aktuator nie może przypadkowo się załączyć.

---

# 39. Power-cycle tests

Każdy projekt powinien przejść wielokrotne:

```text
power off/on
```

bez utraty konfiguracji i bez losowego stanu aktuatorów.

---

# 40. Brownout tests

Jeśli możliwe, testujemy słabe/nagłe zasilanie lub przynajmniej poprawne wykrywanie reset reason.

---

# 41. Watchdog tests

Kontrolowany test watchdog powinien potwierdzić:

- reset reason,
- safe boot sequence,
- brak niebezpiecznego resume.

---

# 42. Crash loop test

Symulujemy firmware restartujące się przed READY.

Sprawdzamy, czy recovery/safe boot działa zgodnie z architekturą.

---

# 43. Memory tests

Dla długiej pracy sprawdzamy:

- free heap,
- min free heap,
- brak stałego spadku pamięci,
- brak fragmentacji powodującej problemy.

---

# 44. Soak test

Dla stabilnego release zalecany jest dłuższy test:

```text
24-72 h
```

na realnym urządzeniu.

Dla krytycznych projektów można testować dłużej.

---

# 45. Network soak

W czasie soak można symulować:

- restart routera,
- utratę Wi-Fi,
- restart brokera,
- restart HA,
- chwilowy brak DNS.

---

# 46. Fault injection

Development build może wspierać kontrolowane symulacje:

```text
sensor fail
MQTT fail
storage fail
critical alarm
actuator fail
time invalid
```

---

# 47. Fault injection nie w produkcyjnym UI

Jeśli mechanizm istnieje, ma być:

- development/service only,
- jasno oznaczony,
- wyłączony domyślnie.

---

# 48. Test fixtures

Wspólne fixture'y powinny być współdzielone tam, gdzie to możliwe.

Przykłady:

```text
FakeClock
FakeStorage
FakeMqttTransport
FakeSensor
FakeActuator
```

---

# 49. Nie kopiujemy test helperów

Jeśli helper jest wspólny dla kilku projektów, trafia do wspólnego test support Core.

---

# 50. Golden config fixtures

Warto przechowywać przykładowe configi dla starszych schema versions do testów migracji.

---

# 51. Golden MQTT cases

Można utrzymywać zestaw oczekiwanych topiców/payloadów dla wspólnych encji.

Pozwala to wykrywać przypadkowe breaking changes.

---

# 52. Public contract tests

Zmiana w:

- MQTT keys,
- alarm IDs,
- HA unique IDs,
- config schema,
- API,

powinna mieć test kontraktu, jeśli jest to element wspólny.

---

# 53. Test naming

Nazwy testów powinny opisywać zachowanie.

Przykład:

```text
critical_alarm_requires_ack_before_recovery
mqtt_reconnect_publishes_full_snapshot
invalid_sensor_blocks_heater
```

---

# 54. Arrange / Act / Assert

Testy powinny być proste i czytelne.

Preferowany układ logiczny:

```text
Arrange
Act
Assert
```

---

# 55. Jeden test = jedno zachowanie

Unikamy ogromnych testów sprawdzających 20 niezależnych rzeczy naraz.

---

# 56. Testy regresji dla bugfixu

Każdy istotny bugfix powinien, jeśli to możliwe, dodać test odtwarzający błąd.

---

# 57. Test przed refaktorem

Przed większym refaktorem działającego modułu warto najpierw dodać testy zachowania.

To szczególnie ważne przy migracji obecnych projektów do `aquaOneCore`.

---

# 58. Migration strategy dla istniejących projektów

Przy przenoszeniu logiki do Core:

```text
capture current behavior
-> add tests
-> extract common code
-> run tests
-> migrate one project
-> verify
-> migrate next
```

Nie przenosimy wszystkich projektów naraz bez punktów kontrolnych.

---

# 59. Build matrix

Core powinien być testowany przynajmniej z reprezentatywnymi platformami używanymi w projektach.

Jeśli większość urządzeń używa jednej rodziny ESP32, nie tworzymy ogromnej macierzy bez potrzeby.

---

# 60. PlatformIO

Jeśli projekty używają PlatformIO, testy i buildy powinny być możliwie powtarzalne przez polecenia projektu.

Przykład:

```text
pio test
pio run
```

Dokładne środowiska zależą od projektu.

---

# 61. CI

GitHub Actions lub inny CI jest zalecany dla:

- build,
- unit tests,
- static checks.

Nie wymagamy hardware tests w zwykłym cloud CI.

---

# 62. CI minimalny

Dla każdego push/PR warto uruchamiać:

```text
Core tests
project build
selected unit tests
```

---

# 63. PR gate

Nie wymagamy formalnego procesu korporacyjnego, ale `main` powinien pozostawać buildowalny.

---

# 64. Main branch

Nie commitujemy świadomie kodu, który:

- nie kompiluje,
- łamie podstawowe testy,
- ma znane krytyczne regresje.

---

# 65. Static analysis

Można używać lekkich narzędzi:

```text
compiler warnings
clang-tidy
cppcheck
```

jeśli nie komplikują nadmiernie workflow.

---

# 66. Warnings

Nowe warningi kompilatora powinny być traktowane poważnie.

Nie ignorujemy ich masowo przez wyłączenie warningów.

---

# 67. Sanitizers

Dla części kodu uruchamianej na PC warto używać sanitizerów, jeśli środowisko na to pozwala.

Nie jest to wymagane dla każdego builda ESP.

---

# 68. Coverage

Coverage może być mierzone dla Core, ale nie jest KPI samym w sobie.

Najważniejsze jest pokrycie ryzykownej logiki.

---

# 69. Test data privacy

Testy i fixture'y nie powinny zawierać prawdziwych:

- haseł,
- tokenów,
- broker credentials.

---

# 70. Release checklist

Każdy release urządzenia powinien przejść skróconą checklistę:

Każda pozycja ma wynik `PASS`, `FAIL` albo `N/A`. Jeśli feature istnieje w produkcie,
odpowiedni test MUSI przejść. `N/A` jest dozwolone wyłącznie z podanym uzasadnieniem, gdy
feature nie jest zaimplementowane, włączone ani wspierane przez dany projekt.

```text
build clean
unit/component tests pass
config migration pass                         # jeśli projekt ma wersjonowaną konfigurację
MQTT smoke pass                               # jeśli projekt używa MQTT
WWW smoke pass                                # jeśli projekt używa Web
time/RTC pass                                 # jeśli projekt używa czasu/RTC
alarm/safety smoke pass                       # jeśli funkcjonalność istnieje lub jest wymagana
OTA pass                                      # jeśli projekt wspiera OTA
power-cycle pass
```

---

# 71. Major release checklist

Przy większej wersji dodatkowo:

```text
full regression
HIL
soak
rollback
migration from supported old versions
diagnostic review
```

---

# 72. Test evidence

Każdy raport MUSI rozróżniać rodzaj dowodu:

- **BUILD** — kompilacja i linkowanie firmware;
- **COMPILE/LINK CHECK** — kompilacja i linkowanie binarium testowego bez wykonania;
- **HOST UNIT TEST** — test jednostkowy wykonany na hoście;
- **DEVICE UNIT TEST** — test jednostkowy wykonany na urządzeniu;
- **INTEGRATION TEST** — wykonany test współpracy modułów;
- **HARDWARE-IN-THE-LOOP (HIL)** — wykonany test z fizycznym sprzętem;
- **ACCEPTANCE TEST** — wykonany scenariusz kryterium odbioru;
- **REGRESSION TEST** — ponowne wykonanie scenariuszy chroniących istniejące zachowanie.

PlatformIO `pio test --without-testing` kompiluje i linkuje test binary, ale wykonuje
**zero runtime test cases**. Taki wynik MUSI być raportowany jako:

```text
compile/link PASS, runtime NOT EXECUTED
```

Nie wolno raportować go jako `tests passed`.

Minimalny test evidence MUSI wskazywać:

- dokładną komendę;
- environment;
- target lub urządzenie;
- datę i commit;
- wynik builda;
- liczbę wykonanych testów;
- liczbę pass/fail/skip;
- hardware i jego rewizję, gdy są istotne.

---

# 73. Hardware test matrix

Każdy projekt powinien mieć prostą listę wspieranych rewizji hardware.

Test release powinien obejmować aktywnie wspieraną rewizję.

---

# 74. Deprecated hardware

Jeśli stara rewizja nie jest już testowana, należy to jawnie zaznaczyć.

---

# 75. Test doubles dla aktuatorów

Logika domenowa powinna móc działać z fake actuatorami.

Pozwala to sprawdzić decyzje safety bez uruchamiania prawdziwej pompy/grzałki.

---

# 76. State-machine tests

Każda złożona state machine powinna mieć testy legalnych i nielegalnych przejść.

---

# 77. Boundary tests

Walidatory powinny być testowane na granicach:

```text
min-1
min
max
max+1
```

---

# 78. Invalid input tests

Testujemy:

- pusty string,
- zły enum,
- NaN,
- infinity,
- za duży payload,
- błędny JSON,
- brak pola,
- duplikat ID.

---

# 79. Duplicate registration tests

Core registries powinny odrzucać:

- duplicate alarm_id,
- duplicate entity key,
- duplicate command key,
- duplicate config namespace,

w przewidywalny sposób.

---

# 80. Resource limit tests

Warto testować zachowanie po przekroczeniu limitów:

- pełny registry,
- za duży JSON,
- brak miejsca storage,
- brak miejsca OTA.

---

# 81. Failure should be explicit

Testy powinny potwierdzać, że błędy są jawne i nie prowadzą do cichego fallbacku, jeśli to niebezpieczne.

---

# 82. No secret leak tests

Automatyczne testy mogą sprawdzać, że API/logi nie zwracają pełnych sekretów.

---

# 83. Restart persistence tests

Testujemy, które dane:

- mają przetrwać,
- nie mają przetrwać.

Przykład:

```text
buzzer_enabled -> persists
mode SERVICE -> does not persist
ordinary ACK -> does not persist
latched alarm -> persists
```

---

# 84. Factory reset persistence tests

Po factory reset potwierdzamy:

- user config removed,
- immutable hardware data preserved,
- buzzer enabled,
- mode NORMAL,
- onboarding required.

---

# 85. Recovery tests

Testujemy recovery priority:

```text
valid config
-> LKG
-> safe defaults if allowed
-> safe state
```

---

# 86. Logging tests

Sprawdzamy:

- correct level,
- correct module,
- ring buffer wrap,
- no flood on stable condition,
- alarm activate/clear/ACK logging.

---

# 87. Performance

Nie potrzebujemy mikrobenchmarków wszędzie.

Dla krytycznych ścieżek sprawdzamy, że:

- loop nie jest blokowany,
- operacje nie zajmują sekund bez powodu,
- polling i WWW nie zagładzają domeny.

---

# 88. Responsiveness HIL

Na sprzęcie warto sprawdzić, że podczas:

- MQTT reconnect,
- WWW request,
- OTA preparation,
- sensor errors,

główna logika nadal reaguje poprawnie.

---

# 89. Flash wear tests

Nie musimy symulować lat flash wear.

Testujemy, że:

- unchanged config nie zapisuje,
- runtime nie zapisuje cyklicznie,
- latched state zapisuje tylko transitions.

---

# 90. Test documentation

Każdy projekt powinien mieć krótki opis:

```text
how to run tests
what requires hardware
what hardware was used
```

---

# 91. Common test commands

Docelowo warto ujednolicić polecenia, np.:

```text
pio test
pio run
```

oraz ewentualny skrypt:

```text
scripts/test_all.py
```

Nie jest to wymagane od razu.

---

# 92. Test ownership

Core testuje mechanizmy wspólne.

Domena testuje:

- własną logikę,
- hazards,
- konfigurację,
- procesy,
- hardware integration.

Nie kopiujemy tych samych testów Core do każdego projektu.

---

# 93. Testowanie standardów

Każdy wspólny standard powinien mieć odpowiadające mu kontrakt tests tam, gdzie zasada jest możliwa do sprawdzenia automatycznie.

---

# 94. Acceptance criteria

Przed implementacją większej funkcji warto spisać krótkie kryteria akceptacji.

Nie wymagamy formalnego BDD, ale jasne zachowanie przed kodowaniem ogranicza regresje.

---

# 95. Definition of done

Funkcja jest ukończona, gdy:

```text
code works
tests cover critical behavior
diagnostics exist
errors are handled
docs updated if contract changed
```

---

# 96. Nie testujemy tylko happy path

Każda ważna funkcja powinna mieć co najmniej jeden test błędu.

---

# 97. Real hardware remains necessary

Nawet bardzo dobre mocki nie zastępują testów realnego:

- boot GPIO,
- Wi-Fi,
- RTC,
- sensorów,
- przekaźników,
- OTA,
- zasilania.

---

# 98. Release freeze dla krytycznej zmiany

Po dużej zmianie Core warto przez krótki etap skupić się na stabilizacji i regresji przed kolejnym dużym refaktorem.

---

# 99. Core API — odpowiedzialność

`aquaOneCore` powinien być projektowany z myślą o testowalności i udostępniać adaptery/interfejsy dla:

```text
Clock
Storage
MqttTransport
Network
Sensor
Actuator
Platform/Ota
```

tam, gdzie jest to uzasadnione.

---

# 100. Domena — odpowiedzialność

Projekt domenowy odpowiada za testy:

- własnych reguł,
- własnych alarmów,
- safe state,
- harmonogramu,
- kalibracji,
- fizycznego hardware.

---

# 101. Minimalny zestaw przed produkcyjnym release

Minimum stosuje tę samą semantykę `PASS` / `FAIL` / uzasadnione `N/A` z rozdziału 70:

```text
Core unit tests PASS
project build PASS
config/migration PASS lub N/A
MQTT smoke PASS lub N/A
WWW smoke PASS lub N/A
time/RTC PASS lub N/A
alarm/safety PASS lub N/A
OTA PASS lub N/A
restart/power-cycle PASS
```

---

# 102. Reguła końcowa

Testy mają umożliwić rozwój wspólnego Core bez strachu, że poprawka w jednym urządzeniu zepsuje pozostałe.

**Najpierw chronimy wspólne kontrakty i safety, potem szczegóły. Testy PC dają szybkość, a HIL potwierdza rzeczywistość.**
