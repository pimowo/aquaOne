# DIAGNOSTICS_STANDARD.md

**Status:** DRAFT
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard diagnostyki dla całego ekosystemu **aquaOne**.

Dotyczy wszystkich urządzeń korzystających z `aquaOneCore`, m.in.:

- `aquaOneLuma`
- `aquaOneDoser`
- `aquaOneHydro`
- `aquaOneClima`
- `aquaOneGas`
- `aquaOneFauna`

Standard określa:

- wspólne dane diagnostyczne,
- status urządzenia,
- health modułów,
- reset reason,
- uptime,
- pamięć,
- sieć,
- MQTT,
- czas,
- storage,
- sensory,
- logowanie i historię,
- zakres danych w WWW i Home Assistant,
- relację diagnostyki z alarmami,
- odpowiedzialność `aquaOneCore` i projektów domenowych.

## Stan implementacji

### Implementation: CURRENT

Core 0.6.2 ma `DiagnosticsService` budujący stały snapshot techniczny z sekcji System, RTC,
Storage oraz opcjonalnie NTP i Network. Doser ma osobny lokalny `DiagnosticsManager`.

### Implementation: TARGET

Dynamiczny rejestr providerów, health wszystkich modułów, ogólny mechanizm revision,
freshness/stale, historia, MQTT, alarmy, Web, heap i diagnostyka domenowa opisane niżej są
modelem docelowym. Nie są obecnym API Core.

Granice odpowiedzialności:

- **Logging** zapisuje przebieg i zdarzenia wykonania;
- **Diagnostics** opisuje bieżący stan techniczny, health i snapshot;
- **Alarm** opisuje stan wymagający reakcji według [ALARM_STANDARD.md](ALARM_STANDARD.md).

Diagnostyka nie kopiuje lifecycle alarmu ani reguł bezpieczeństwa z
[SAFETY_STANDARD.md](SAFETY_STANDARD.md).

---

# 2. Zasada nadrzędna

Diagnostyka ma pomagać odpowiedzieć na pytania:

```text
Czy urządzenie działa?
Jeśli nie działa — dlaczego?
Który moduł jest problemem?
Czy problem jest chwilowy, czy trwały?
Czy urządzenie może dalej bezpiecznie pracować?
```

Diagnostyka ma być:

- lekka,
- czytelna,
- lokalna,
- dostępna bez Internetu,
- niezależna od HA,
- przydatna podczas serwisu,
- spójna we wszystkich projektach aquaOne.

---

# 3. Diagnostyka ≠ alarm

Diagnostyka i alarmy są rozdzielone.

Diagnostyka opisuje stan techniczny.

Alarm oznacza problem wymagający uwagi, ograniczenia działania lub reakcji bezpieczeństwa.

Przykład:

```text
MQTT disconnected
```

to zwykle diagnostyka.

Jeśli konkretny projekt nie może bez MQTT bezpiecznie wykonać wymaganej funkcji, dopiero wtedy domena może utworzyć odpowiedni alarm.

---

# 4. Wspólny status

Każde urządzenie ma wspólny status:

```text
OK
WARNING
ERROR
```

`status` jest stanem zagregowanym.

Nie oznacza wyłącznie alarmów.

Może uwzględniać:

- alarmy,
- krytyczne błędy modułów,
- problemy storage,
- błędy inicjalizacji,
- inne problemy techniczne.

---

# 5. Reguła statusu

Status wyliczany jest przez Core jako najwyższy aktualny poziom problemu.

Ogólna zasada:

```text
brak problemu -> OK
problem ostrzegawczy -> WARNING
poważny problem -> ERROR
```

Alarm `CRITICAL` mapuje do `status = ERROR`, a jego dokładne severity pozostaje dostępne przez `alarm_severity`.

---

# 6. Jedno źródło statusu

Projekt domenowy nie publikuje własnego konkurencyjnego globalnego statusu.

Core agreguje stan z:

- alarm managera,
- health modułów,
- storage,
- time,
- network,
- MQTT,
- domeny.

---

# 7. Health modułów

Każdy ważny moduł może raportować prosty health:

```text
OK
DEGRADED
ERROR
INIT
DISABLED
```

Znaczenie:

## OK

Moduł działa poprawnie.

## DEGRADED

Moduł działa częściowo lub w trybie ograniczonym.

## ERROR

Moduł nie działa poprawnie.

## INIT

Moduł jest jeszcze w trakcie inicjalizacji.

## DISABLED

Moduł został świadomie wyłączony konfiguracją i nie jest błędem.

---

# 8. INIT nie jest alarmem

`INIT` nie powinien automatycznie powodować WARNING/ERROR podczas normalnej fazy startowej.

Dopiero przekroczenie oczekiwanego czasu inicjalizacji może zostać uznane za problem.

---

# 9. DISABLED nie jest błędem

Jeśli moduł jest świadomie wyłączony:

```text
health = DISABLED
```

Nie wpływa negatywnie na globalny status.

---

# 10. Diagnostyka Core

Core powinien zapewniać diagnostykę co najmniej dla:

```text
system
network
mqtt
time
storage
alarms
web
```

Projekt domenowy może rejestrować dodatkowe moduły.

---

# 11. Diagnostyka domenowa

Projekt może raportować health np.:

```text
sensor_temperature
sensor_level
pump
rtc
co2_valve
led_driver
feeder
```

Nie wszystkie elementy muszą być osobnymi encjami HA.

---

# 12. Wspólne pola systemowe

Każde urządzenie powinno mieć co najmniej:

```text
status
uptime
firmware_version
core_version
reset_reason
```

Jeśli dostępne:

```text
hardware_revision
config_schema_version
mqtt_protocol_version
```

---

# 13. uptime

`uptime` jest czasem od ostatniego restartu.

Nie jest zapisywany trwale.

Rekomendowana jednostka:

```text
seconds
```

Do UI można go formatować jako dni/godziny/minuty.

---

# 14. reset_reason

Core powinien odczytać i znormalizować przyczynę ostatniego restartu.

Wspólne wartości logiczne mogą obejmować:

```text
POWER_ON
SOFTWARE
WATCHDOG
BROWNOUT
PANIC
DEEP_SLEEP
OTA
UNKNOWN
```

Dokładne mapowanie z ESP-IDF/Arduino pozostaje wewnątrz Core.

---

# 15. reset_reason a alarm

Sam poprzedni restart watchdog/panic nie musi tworzyć aktywnego alarmu.

Powinien być:

- widoczny diagnostycznie,
- zapisany w logu,
- uwzględniony przy analizie stabilności.

Powtarzające się restarty mogą zostać wykryte jako osobny problem.

---

# 16. Boot counter

Core może przechowywać lekki licznik uruchomień:

```text
boot_count
```

Nie jest obowiązkowy dla HA.

Może pomagać w diagnostyce restartów.

---

# 17. Crash loop detection

Jeśli urządzenie wielokrotnie restartuje się w krótkim czasie, Core powinien móc wykryć podejrzenie crash loop.

Reakcja może obejmować:

- log,
- diagnostyczny ERROR,
- ograniczony safe boot,
- brak uruchomienia niebezpiecznej domeny.

Szczegółowe zasady mogą zostać rozwinięte w `SAFETY_STANDARD`.

---

# 18. Pamięć

Core powinien udostępniać co najmniej:

```text
free_heap
min_free_heap
```

Jeśli platforma wspiera:

```text
largest_free_block
psram_free
psram_total
```

mogą być dostępne diagnostycznie.

---

# 19. Pamięć a HA

Dane pamięci nie muszą domyślnie trafiać do HA.

Preferowane miejsce:

```text
WWW -> Diagnostics
```

HA powinien pozostać czysty.

---

# 20. Niski heap

Niski heap sam w sobie nie musi być alarmem.

Core może raportować:

```text
memory_health = DEGRADED
```

jeśli wartość spadnie poniżej bezpiecznego progu.

Jeśli dalsze działanie staje się niebezpieczne, może dojść do ERROR / alarmu domenowego/systemowego.

---

# 21. Wi-Fi diagnostyka

Core powinien udostępniać:

```text
wifi_state
wifi_rssi
ip_address
ssid
```

Opcjonalnie:

```text
channel
bssid
disconnect_reason
reconnect_count
```

---

# 22. wifi_state

Wspólny enum:

```text
DISCONNECTED
CONNECTING
CONNECTED
```

Jeśli istnieje tryb AP:

```text
AP
STA_AP
```

może być raportowany dodatkowo.

---

# 23. Brak Wi-Fi

Brak Wi-Fi nie powoduje automatycznie alarmu ani globalnego ERROR, jeśli urządzenie może nadal autonomicznie działać.

Może wpływać na diagnostykę jako:

```text
DEGRADED
```

jeśli funkcje sieciowe są skonfigurowane i oczekiwane.

---

# 24. RSSI

`wifi_rssi` jest publikowany jako liczba dBm.

Nie tworzymy osobnego alarmu dla słabego RSSI.

Można oznaczyć health sieci jako DEGRADED, jeśli sygnał jest bardzo słaby.

---

# 25. MQTT diagnostyka

Core powinien udostępniać:

```text
mqtt_enabled
mqtt_state
mqtt_broker_configured
mqtt_last_error
mqtt_reconnect_count
```

Dodatkowo:

```text
mqtt_root
mqtt_protocol_version
```

---

# 26. mqtt_state

Wspólny enum:

```text
DISABLED
DISCONNECTED
CONNECTING
SYNCING
ONLINE
ERROR
```

`ONLINE` oznacza pełną gotowość zgodną z `MQTT_STANDARD`, a nie tylko zestawiony TCP socket.

---

# 27. MQTT SYNCING

Stan:

```text
SYNCING
```

oznacza, że transport jest połączony, ale snapshot / subscriptions / wymagane PUBACK jeszcze nie zakończyły pełnej synchronizacji.

Urządzenie nie powinno być jeszcze traktowane jako w pełni online.

---

# 28. mqtt_last_error

Core może przechowywać krótki techniczny kod ostatniego błędu, np.:

```text
DNS_FAILED
TCP_FAILED
AUTH_FAILED
TIMEOUT
PROTOCOL_ERROR
PUBACK_TIMEOUT
```

Bez długich tekstów dynamicznych.

---

# 29. MQTT reconnect_count

`mqtt_reconnect_count` jest runtime counter.

Nie wymaga zapisu trwałego.

Służy do diagnostyki niestabilnego połączenia.

---

# 30. Time diagnostyka

Jeśli urządzenie używa czasu, Core powinien udostępniać:

```text
time_state
time_valid
rtc_state
ntp_state
last_time_sync
```

Nie każde urządzenie musi posiadać RTC lub NTP.

---

# 31. time_state

Rekomendowany enum:

```text
INVALID
RTC
NTP_SYNCED
RTC_SYNCED
DEGRADED
```

Dokładna semantyka może zależeć od modułu czasu, ale powinna być wspólna dla wszystkich projektów.

---

# 32. time_valid

`time_valid` odpowiada tylko na pytanie:

```text
czy aktualny czas jest wystarczająco wiarygodny do działania?
```

To prosty boolean.

---

# 33. Brak poprawnego czasu

Jeśli projekt nie potrzebuje czasu do działania, brak NTP/RTC nie musi wpływać na status.

Jeśli harmonogramy zależą od czasu, domena może podnieść WARNING/ERROR zależnie od konsekwencji.

---

# 34. Storage diagnostyka

Zgodnie z `CONFIG_STORAGE_STANDARD` Core powinien udostępniać:

```text
storage_state
config_schema_version
config_loaded
config_migrated
```

---

# 35. storage_state

Rekomendowany enum:

```text
OK
RECOVERED
DEGRADED
ERROR
```

---

# 36. RECOVERED

`RECOVERED` oznacza, że wykryto problem, ale Core skutecznie odzyskał poprawny stan np. z `last_known_good`.

Powinno to być widoczne diagnostycznie i zalogowane.

Nie musi utrzymywać ERROR, jeśli urządzenie działa poprawnie.

---

# 37. Web diagnostyka

Core może udostępniać:

```text
web_state
web_request_errors
```

Domyślnie nie ma potrzeby wystawiania tego do HA.

---

# 38. Sensor diagnostics

Sensor powinien móc raportować co najmniej:

```text
VALID
INVALID
INIT
DISCONNECTED
ERROR
```

Domena może mapować te stany do własnych alarmów.

---

# 39. Ostatnia poprawna próbka

Dla sensorów przydatne może być:

```text
last_valid_sample_age
```

czyli ile czasu minęło od ostatniego poprawnego odczytu.

Nie musi być wspólną encją HA.

---

# 40. Brak używania starej wartości jako aktualnej

Jeśli sensor jest invalid, diagnostyka musi to jasno pokazać.

Nie wolno prezentować starej wartości w sposób sugerujący, że jest aktualna.

Jeśli UI pokazuje ostatnią wartość, musi oznaczyć ją jako stale/last valid.

---

# 41. Command diagnostics

Core może rejestrować odrzucone komendy, np.:

```text
command_rejected
reason=safety_lock
```

Nie każda odmowa musi tworzyć alarm.

---

# 42. Powód odrzucenia

Rekomendowane techniczne reason codes:

```text
SAFETY_LOCK
INVALID_STATE
INVALID_VALUE
NOT_READY
DISABLED
BUSY
UNSUPPORTED
```

---

# 43. Liczniki błędów

Core może utrzymywać runtime counters, np.:

```text
web_error_count
mqtt_error_count
sensor_error_count
command_reject_count
```

Nie powinny być automatycznie zapisywane do flash.

---

# 44. Error counters a HA

Liczniki błędów domyślnie pozostają w WWW Diagnostics.

Do HA wystawiamy tylko te, które naprawdę pomagają użytkownikowi.

---

# 45. Logger

Wspólny logger Core jest podstawowym źródłem krótkiej historii technicznej.

Poziomy:

```text
DEBUG
INFO
WARN
ERROR
```

`CRITICAL` z systemu alarmowego może być logowany jako `ERROR` z zachowaniem severity alarmu w treści/metadanych.

---

# 46. Log format

Wpis powinien mieć logicznie:

```text
timestamp
level
module
message/event
```

Opcjonalnie:

```text
code
context
```

Nie wymagamy ciężkich struktur JSON wewnątrz urządzenia.

---

# 47. Moduły loggera

Przykładowe techniczne nazwy:

```text
SYSTEM
NETWORK
MQTT
TIME
STORAGE
ALARM
WEB
DOMAIN
SENSOR
```

Projekt może dodawać własne stabilne moduły.

---

# 48. Brak sekretów w logach

Nigdy nie logujemy:

- haseł Wi-Fi,
- haseł MQTT,
- tokenów,
- kluczy,
- pełnych danych uwierzytelniających.

---

# 49. Lokalna historia logów

Core może przechowywać lekki ring buffer ostatnich logów.

Rekomendowany rozmiar:

```text
50-100 wpisów
```

Dokładny limit zależy od RAM urządzenia.

Nie wymagamy trwałości po restarcie.

---

# 50. Log history w WWW

WWW Diagnostics może pokazywać:

```text
time
level
module
message
```

Bez rozbudowanego systemu log management.

---

# 51. Log level

Można wspierać konfigurowalny poziom logowania:

```text
INFO
WARN
ERROR
DEBUG
```

`DEBUG` powinien być używany głównie podczas serwisu/development.

---

# 52. DEBUG nie jako domyślny production mode

Domyślny poziom produkcyjny:

```text
INFO
```

lub równoważny lekki zestaw.

Nie zalewamy urządzenia debug logami podczas normalnej pracy.

---

# 53. Runtime debug mode

Można umożliwić tymczasowe włączenie DEBUG z WWW.

Preferowane:

- bez restartu,
- opcjonalny timeout,
- brak trwałości lub świadoma trwałość.

Szczegóły mogą zostać dopracowane później.

---

# 54. Historia alarmów a logi

Historia alarmów z `ALARM_STANDARD` i ogólny logger są rozdzielone.

Alarm history:
- ma mały, jednoznaczny ring buffer alarm events.

Logger:
- ma szerszą historię techniczną.

Nie duplikujemy całych struktur.

---

# 55. Diagnostyka WWW

Sekcja Diagnostics powinna grupować dane logicznie:

```text
System
Memory
Network
MQTT
Time
Storage
Modules
Sensors
Logs
```

Pokazujemy tylko grupy dostępne na danym urządzeniu.

---

# 56. Dashboard vs Diagnostics

Dashboard pokazuje tylko najważniejsze dane.

Diagnostics zawiera szczegóły techniczne.

Nie przenosimy całej diagnostyki na ekran główny.

---

# 57. Diagnostyka w HA

Wspólne encje diagnostyczne HA powinny pozostać ograniczone.

Rekomendowany zestaw:

```text
status
wifi_rssi
ip_address
uptime
firmware_version
core_version
restart
```

Dodatkowe encje tylko wtedy, gdy są użyteczne dla użytkownika.

---

# 58. Entity category

Techniczne encje HA powinny, jeśli HA Discovery to wspiera, używać:

```text
entity_category = diagnostic
```

np.:

```text
wifi_rssi
ip_address
uptime
firmware_version
core_version
```

---

# 59. Status jako główna encja

`status` nie powinien być ukryty jako czysta diagnostyka, ponieważ jest główną informacją o stanie urządzenia.

---

# 60. Alarmy w HA

Alarmy są wystawiane zgodnie z `ALARM_STANDARD`, a nie przez osobne mechanizmy diagnostyczne.

Nie duplikujemy `alarm_active`, `alarm_severity`, `safety_lock` jako nowych diagnostic sensors.

---

# 61. Network/MQTT health w HA

Nie tworzymy domyślnie osobnych:

```text
wifi_connected
mqtt_connected
online
```

jeżeli nie ma potrzeby.

Availability pozostaje prawdą o dostępności MQTT urządzenia.

Szczegółowe stany są dostępne w WWW Diagnostics.

---

# 62. Brak last_seen jako obowiązkowej encji

Nie tworzymy obowiązkowego `last_seen`.

Availability i stan urządzenia są wystarczające.

---

# 63. Self-test przy starcie

Core może wykonywać lekki self-test przy boot.

Powinien sprawdzić tylko elementy, które da się wiarygodnie ocenić bez ryzyka.

Przykłady:

```text
storage readable
config valid
required modules initialized
critical sensors available
time module started
```

---

# 64. Self-test nie może blokować bez końca

Każdy test startowy musi mieć timeout/grace period.

Urządzenie nie może wisieć bez końca podczas init.

---

# 65. Ready state

Urządzenie jest `ready` dopiero po:

- załadowaniu configu,
- migracji,
- podstawowej walidacji,
- inicjalizacji wymaganych modułów,
- pierwszej wiarygodnej ocenie alarmów.

---

# 66. Ready a MQTT online

MQTT może osiągnąć `ONLINE` dopiero, gdy urządzenie jest gotowe zgodnie z wymaganiami `MQTT_STANDARD`.

---

# 67. Boot phase

Core może raportować lekki boot phase:

```text
STARTING
CONFIG
SERVICES
DOMAIN
READY
```

To diagnostyka runtime.

Nie musi być encją HA.

---

# 68. Nieprawidłowy init modułu

Jeśli opcjonalny moduł nie wystartuje:

```text
health = ERROR lub DEGRADED
```

ale urządzenie może działać dalej, jeśli moduł nie jest wymagany.

Jeśli moduł jest krytyczny, domena lub Core może zablokować normalną pracę.

---

# 69. Required vs optional modules

Projekt powinien jasno określić, czy moduł jest:

```text
REQUIRED
OPTIONAL
```

Błąd REQUIRED ma większy wpływ na globalny status.

---

# 70. Degraded mode

Urządzenie może działać w trybie ograniczonym, jeśli błąd nie zagraża bezpieczeństwu.

Wtedy:

```text
module health = DEGRADED/ERROR
global status = WARNING lub ERROR
```

zależnie od znaczenia problemu.

---

# 71. Watchdog diagnostics

Jeśli watchdog jest używany, Core powinien umożliwić diagnostykę resetów watchdog.

Nie tworzymy skomplikowanego runtime monitora watchdog jako części DIAGNOSTICS_STANDARD.

---

# 72. Brownout

Reset brownout powinien być widoczny jako `reset_reason = BROWNOUT`.

To cenna informacja przy problemach z zasilaniem.

---

# 73. Panic/crash

Po restarcie z powodu panic Core powinien:

- oznaczyć reset_reason,
- zalogować dostępne informacje,
- nie przechowywać ogromnych dumpów w NVS bez potrzeby.

Jeśli platforma wspiera trwały crash marker, może być użyty lekko.

---

# 74. Ostatni błąd systemowy

Core może utrzymywać:

```text
last_system_error
```

jako krótki kod techniczny.

Nie powinien zawierać wielkiej dynamicznej wiadomości.

---

# 75. Timestamp diagnostyczny

Jeśli czas systemowy jest valid, logi i zdarzenia używają rzeczywistego timestampu.

Jeśli czas nie jest jeszcze valid, można użyć:

```text
uptime-based timestamp
```

Nie fałszujemy daty.

---

# 76. Wskaźnik czasu w logach

UI może oznaczyć, czy wpis ma:

```text
absolute time
uptime time
```

jeśli ma to znaczenie dla diagnostyki.

---

# 77. Diagnostyka storage wear

Nie wymagamy dokładnego licznika zużycia flash.

Wystarczy, że architektura minimalizuje zapisy zgodnie z `CONFIG_STORAGE_STANDARD`.

---

# 78. Diagnostyka restartu ręcznego

Kontrolowany restart z WWW/HA/MQTT powinien być logowany przed restartem.

Jeśli możliwe, Core zapisuje krótki restart marker pozwalający później rozpoznać `SOFTWARE`.

---

# 79. OTA diagnostics

Po OTA powinny być dostępne co najmniej:

```text
firmware_version
reset_reason/boot reason
ota state/result
```

Szczegóły określi `OTA_STANDARD`.

---

# 80. Liczniki połączeń

Runtime counters typu reconnect mogą być zerowane po restart.

Nie ma potrzeby utrwalania ich w flash.

---

# 81. Snapshot diagnostyczny

WWW może mieć akcję:

```text
Download diagnostics
```

która generuje lekki snapshot tekstowy/JSON zawierający wyłącznie dane techniczne bez sekretów.

Nie jest to obowiązkowe w pierwszej wersji Core, ale architektura powinna to umożliwiać.

---

# 82. Zawartość snapshotu

Snapshot może zawierać:

```text
device type
versions
reset reason
uptime
status
module health
memory
network state
mqtt state
time state
storage state
active alarms summary
recent logs
```

Nie zawiera sekretów.

---

# 83. Support bundle

Pełny support bundle z dużą historią nie jest wymagany.

ESP powinno pozostać lekkie.

---

# 84. Rejestr modułów diagnostycznych

Core powinien umożliwiać rejestrację health providerów.

Przykład logiczny:

```text
registerHealthProvider(...)
```

Dzięki temu dashboard diagnostyczny nie jest zakodowany pod konkretne urządzenie.

---

# 85. Stabilne kody błędów

Kody diagnostyczne powinny być:

- krótkie,
- techniczne,
- stabilne,
- po angielsku,
- `UPPER_SNAKE_CASE`.

Przykład:

```text
RTC_NOT_FOUND
MQTT_AUTH_FAILED
CONFIG_INVALID
SENSOR_TIMEOUT
```

---

# 86. Tekst dla użytkownika

UI może mapować kod techniczny na czytelny opis.

Kod pozostaje stabilnym kontraktem dla logiki i debugowania.

---

# 87. Brak dynamicznych wielkich komunikatów

Nie generujemy długich wyjątków i opisów w RAM.

Preferowane:

```text
error_code + krótki opis
```

---

# 88. Diagnostyka a bezpieczeństwo

Diagnostyka nigdy nie może:

- omijać safety_lock,
- zmieniać hardware bez jawnej akcji,
- automatycznie kasować alarmów,
- wyłączać zabezpieczeń.

---

# 89. Testy diagnostyki

Należy testować co najmniej:

- status aggregation,
- module health,
- init timeout,
- reset reason mapping,
- MQTT states,
- storage states,
- invalid sensor,
- log ring buffer,
- brak sekretów,
- recovery state.

---

# 90. Core API — odpowiedzialność

**Implementation: TARGET.** `aquaOneCore` POWINNO docelowo zapewnić mechanizmy odpowiadające za:

```text
DiagnosticsManager
HealthRegistry
SystemDiagnostics
NetworkDiagnostics
MqttDiagnostics
TimeDiagnostics
StorageDiagnostics
LogBuffer
ErrorCodeRegistry
```

Nazwy klas mogą się różnić, ale podział odpowiedzialności powinien pozostać.

---

# 91. Domena — odpowiedzialność

Projekt domenowy odpowiada za:

- health swoich sensorów i aktuatorów,
- własne error codes,
- określenie REQUIRED/OPTIONAL,
- wpływ problemu na działanie,
- ewentualne przejście diagnostyki w alarm.

---

# 92. Reguła końcowa

Diagnostyka ma być na tyle szczegółowa, aby znaleźć problem, ale na tyle lekka, aby sama nie stała się problemem.

**Core CURRENT zapewnia stały snapshot wybranych modułów technicznych. Rozszerzalny health,
rejestr providerów i diagnostyka domenowa pozostają TARGET. Domena definiuje znaczenie swoich
sensorów i aktuatorów.**
