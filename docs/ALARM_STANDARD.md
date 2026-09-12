# ALARM_STANDARD.md

**Status:** DRAFT
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard alarmów dla całego ekosystemu **aquaOne**.

Dotyczy wszystkich urządzeń korzystających z `aquaOneCore`, m.in.:

- `aquaOneLuma`
- `aquaOneDoser`
- `aquaOneHydro`
- `aquaOneClima`
- `aquaOneGas`
- `aquaOneFauna`

Standard określa poziomy ważności alarmów, model stanu alarmu, ACK i ACK ALL, alarmy `latched`, alarmy `CRITICAL`, safe state i `safety_lock`, buzzer i globalne wyciszenie dźwięków, opóźnienia, histerezę i startup grace period, wspólne encje Home Assistant, zachowanie po restarcie, eventy MQTT, historię alarmów w WWW oraz rolę `aquaOneCore` i projektów domenowych.

## Stan implementacji

### Implementation: CURRENT

Nie istnieje jeszcze wspólny framework alarmów w AquaCore. Poszczególne projekty mogą mieć
lokalne mechanizmy alarmowe; ich obecność nie oznacza zgodności z całym tym standardem.

### Implementation: TARGET

Lifecycle, severity, timestamps, latch, ACK, deduplikacja, historia i reprezentacja
transportowa opisane w tym dokumencie są kontraktem docelowym Core. Domena definiuje klucz
alarmu, znaczenie, progi, reakcję bezpieczeństwa i warunki recovery.

### Implementation: FUTURE

Akcje alarmowe Home Assistant i MQTT są poza pierwszym read-only kontraktem MQTT Dosera.
Mogą zostać wdrożone dopiero jako jawnie zatwierdzone rozszerzenie
[MQTT_STANDARD.md](MQTT_STANDARD.md).

## 2. Zasada nadrzędna

System alarmowy ma być lokalny, autonomiczny, lekki dla ESP, niezależny od Home Assistant i MQTT, przewidywalny, bezpieczny i spójny we wszystkich urządzeniach aquaOne.

Docelowo `aquaOneCore` ma zapewniać mechanikę alarmów. Projekt domenowy definiuje kiedy alarm ma się aktywować, jakie ma znaczenie, severity, czy używa buzzera, czy jest latched, jaki safe state wywołuje i jakie funkcje blokuje.

## 3. Poziomy severity

Wspólny zestaw:

```text
WARNING
ERROR
CRITICAL
```

`WARNING` oznacza problem wymagający uwagi, ale bez konieczności zatrzymania pracy. `ERROR` oznacza poważny problem mogący ograniczyć lub zablokować część funkcji. `CRITICAL` oznacza stan wymagający natychmiastowej reakcji bezpieczeństwa i wejścia w domenowy safe state.

## 4. Mapowanie na wspólny status

Wspólny `state/status` pozostaje:

```text
OK
WARNING
ERROR
```

Mapowanie:

```text
brak alarmów -> OK
WARNING      -> WARNING
ERROR        -> ERROR
CRITICAL     -> ERROR
```

Aby zachować informację o `CRITICAL`, HA otrzymuje osobny sensor `alarm_severity` z wartościami:

```text
NONE
WARNING
ERROR
CRITICAL
```

## 5. Model alarmu

Każdy alarm ma co najmniej:

```text
alarm_id
severity
active
acknowledged
latched
audible
category
priority
```

Opcjonalnie:

```text
description
recommended_action
source_entity_key
delay_on
delay_off
hysteresis
suppressed_in_service
startup_grace_period
```

## 6. alarm_id

Każdy alarm posiada stały techniczny identyfikator `snake_case`, np.:

```text
water_level_low
sensor_fault
overpressure
storage_error
co2_pressure_high
```

`alarm_id` jest wspólnym identyfikatorem używanym przez logikę, WWW, MQTT eventy, diagnostykę i HA.

## 7. Stan aktywności i ACK

Stan alarmu i ACK są rozdzielone:

```text
active = true/false
acknowledged = true/false
```

`active` mówi, czy problem aktualnie trwa. `acknowledged` mówi, czy użytkownik potwierdził bieżące wystąpienie.

ACK nie usuwa aktywnej przyczyny.

## 8. Domyślne auto-clear

Domyślnie alarm jest `auto-clear` i znika po ustąpieniu przyczyny.

Tylko wybrane alarmy są `latched` i wymagają świadomego ACK po ustąpieniu przyczyny.

## 9. ACK

ACK:
- nie usuwa trwającej przyczyny,
- potwierdza bieżące wystąpienie,
- wycisza buzzer dla bieżącego wystąpienia,
- pozostawia alarm aktywny w WWW/HA, jeśli przyczyna nadal trwa.

## 10. Ponowne wystąpienie

Jeśli alarm był ACK, ustąpił i pojawił się ponownie, nowe wystąpienie zaczyna z:

```text
acknowledged = false
```

## 11. Alarmy latched

Alarm `latched` po ustąpieniu fizycznej przyczyny nie znika automatycznie.

Core rozróżnia:

```text
condition_active
latched_active
```

Jeśli przyczyna nadal trwa, ACK tylko potwierdza i wycisza. Jeśli przyczyna ustąpiła, ACK kasuje latch i alarm znika.

Nie ma osobnego globalnego `RESET ALARM`.

## 12. Trwałość latched

Alarmy `latched` przetrwają restart ESP. Trwale zapisujemy tylko minimalne dane potrzebne do odtworzenia latcha.

Po restarcie `latched` wraca jako:

```text
acknowledged = false
```

Szczegóły storage definiuje `CONFIG_STORAGE_STANDARD`.

## 13. CRITICAL zawsze latched

Każdy alarm `CRITICAL` jest automatycznie traktowany jako `latched`. Projekt domenowy nie może tego wyłączyć.

## 14. Safe state

Każdy alarm `CRITICAL` wymusza lokalny safe state niezależnie od MQTT, HA, Wi-Fi i WWW.

Docelowy Core koordynuje stan alarmu, a projekt domenowy definiuje i wykonuje konkretną reakcję, np. zatrzymanie pompy, dozowania, CO2 lub grzania.

## 15. Safe state zależny od alarmu

Nie istnieje jedna sztywna reakcja safe state dla całego urządzenia. Różne alarmy `CRITICAL` mogą wymuszać różne działania, a jeśli kilka `CRITICAL` jest aktywnych równocześnie, reakcje bezpieczeństwa się sumują.

## 16. Brak automatycznego restartu ESP

Alarm `CRITICAL` nie restartuje automatycznie ESP. Urządzenie pozostaje uruchomione dla diagnostyki i nie może wpaść w pętlę restartów.

## 17. safety_lock

System posiada wspólny stan:

```text
safety_lock
```

`safety_lock = ON` oznacza, że urządzenie jest zablokowane przez aktywny lub zatrzaśnięty alarm `CRITICAL` i nie może jeszcze wrócić do normalnej pracy.

Stan jest widoczny w WWW i jako `binary_sensor` w HA.

## 18. safety_lock po ustąpieniu przyczyny

Jeśli przyczyna `CRITICAL` ustąpiła, ale alarm nadal jest zatrzaśnięty i czeka na ACK:

```text
safety_lock = ON
```

Blokada znika dopiero gdy:
1. ustąpią wszystkie blokujące przyczyny,
2. spełnione zostaną domenowe warunki bezpiecznego wznowienia,
3. użytkownik wykona wymagany ACK.

## 19. Wznowienie po CRITICAL

Po `CRITICAL` urządzenie nie wraca automatycznie do normalnej pracy.

Wznowienie wymaga:
- ustąpienia przyczyny,
- spełnienia domenowego warunku bezpiecznego wznowienia,
- świadomego ACK użytkownika.

Nie ma osobnego przycisku `Resume` ani `Clear Safety Lock`. Do tego służy `ACK ALL`.

## 20. ERROR i WARNING po ustąpieniu

Domyślnie `WARNING` i `ERROR` mogą automatycznie wrócić do normalnego stanu po ustąpieniu przyczyny. Jeśli konkretny alarm ma wymagać świadomego potwierdzenia, projekt oznacza go jako `latched`.

Nie istnieje osobny parametr `requires_ack`.

## 21. Blokowanie sterowania przy safety_lock

Przy aktywnym `safety_lock` Core może blokować akcje, które mogłyby uruchomić niebezpieczną funkcję.

Dotyczy wszystkich źródeł:
- MQTT,
- HA,
- WWW,
- fizyczne przyciski.

Projekt domenowy deklaruje, które akcje są blokowane.

Zablokowana komenda nie jest wykonywana, nie zmienia state i nie jest kolejkowana.

## 22. Akcje dozwolone przy safety_lock

`safety_lock` nie blokuje:
- ACK,
- ACK ALL,
- diagnostyki,
- akcji potrzebnych do usunięcia problemu,
- akcji jawnie oznaczonych jako bezpieczne.

## 23. ACK ALL

System posiada wspólną akcję `ACK ALL` dostępną przez WWW, HA i fizyczny przycisk ACK.

ACK ALL:
- potwierdza wszystkie aktywne alarmy,
- wycisza bieżący buzzer,
- kasuje `latched`, których przyczyna już ustąpiła,
- może zdjąć `safety_lock`, jeśli wszystkie warunki bezpieczeństwa są spełnione.

## 24. Fizyczny przycisk ACK

Jeżeli projekt definiuje taki przycisk zgodnie z własnym kontraktem domenowym, krótki klik
MOŻE działać jak `ACK ALL` i ustawiać:

```text
mode = NORMAL
```

Core używa jednej wspólnej implementacji ACK niezależnie od źródła.

## 25. ACK pojedynczego alarmu

WWW umożliwia ACK pojedynczego alarmu przy jego wpisie.

HA nie tworzy osobnego przycisku ACK dla każdego alarmu. W HA istnieje wspólny `ACK ALL`.

## 26. Globalne ustawienie buzzera

System posiada trwałe ustawienie:

```text
buzzer_enabled
```

Dostępne przez WWW i HA. Oba interfejsy sterują dokładnie tym samym stanem.

## 27. Trwałość buzzer_enabled

`buzzer_enabled`:
- przetrwa restart,
- przetrwa zanik zasilania,
- przetrwa aktualizację firmware,
- pozostaje takie, jakie ustawił użytkownik, aż sam je zmieni.

Na świeżym urządzeniu / po factory reset:

```text
buzzer_enabled = ON
```

## 28. Całkowite wyłączenie dźwięku

Jeśli:

```text
buzzer_enabled = OFF
```

buzzer nie jest używany w ogóle, również dla `CRITICAL`.

Wyłączenie dźwięku nie wpływa na alarm, status, safety_lock, safe state, HA ani WWW.

## 29. Natychmiastowa reakcja na zmianę buzzera

Zmiana na `OFF` natychmiast zatrzymuje buzzer.

Zmiana na `ON` podczas trwającego niepotwierdzonego alarmu dźwiękowego natychmiast uruchamia odpowiedni wzorzec. Jeśli wszystkie alarmy są już ACK, samo ponowne włączenie buzzera ich nie odtwarza.

## 30. audible per alarm

Każdy alarm deklaruje:

```text
audible = true/false
```

Dźwięk działa tylko gdy:

```text
alarm.audible == true
AND
buzzer_enabled == true
AND
alarm.acknowledged == false
```

## 31. Wspólne wzorce buzzera

Core definiuje trzy wspólne lekkie wzorce:

```text
WARNING
ERROR
CRITICAL
```

Projekty domenowe nie tworzą własnych melodii.

Wzorce mają być:
- nieblokujące,
- proste,
- rozpoznawalne,
- bez `delay()`.

## 32. Priorytet buzzera

Jeśli aktywnych jest kilka alarmów dźwiękowych, Core gra tylko wzorzec alarmu o najwyższej ważności:

```text
CRITICAL > ERROR > WARNING
```

W obrębie tego samego severity używany jest wewnętrzny priorytet alarmu.

## 33. ACK i buzzer

ACK bieżącego alarmu dźwiękowego wycisza go dla tego wystąpienia, ale nie wyłącza globalnego `buzzer_enabled`.

Nowy alarm dźwiękowy może ponownie uruchomić buzzer.

## 34. Kategorie alarmów

Wspólny enum kategorii:

```text
sensor
safety
network
storage
process
hardware
configuration
time
other
```

Kategoria służy do WWW, diagnostyki i grupowania.

## 35. Priorytet alarmu

Każdy alarm może mieć prosty wewnętrzny priorytet liczbowy.

Severity zawsze ma pierwszeństwo. Priorytet służy tylko do rozstrzygania kolejności w obrębie tego samego severity. Przy remisie obowiązuje stała kolejność rejestracji.

## 36. description

Każdy alarm może mieć krótki stały opis techniczny `description`, widoczny w WWW i logach, ale nie jako osobna encja HA.

## 37. recommended_action

Alarm może mieć opcjonalne `recommended_action`, np.:

```text
Sprawdź czujnik poziomu
Uzupełnij wodę
Sprawdź ciśnienie CO2
```

Pole jest krótkie, stałe, widoczne w WWW i nie jest osobną encją HA.

## 38. source_entity_key

Alarm może opcjonalnie wskazywać powiązaną encję/sensor:

```text
source_entity_key
```

Core nie kopiuje wartości do alarmu. WWW może pobrać aktualną wartość z istniejącego stanu.

## 39. Alarm vs event

Alarm oznacza realny problem wymagający uwagi, reakcji, ograniczenia działania lub reakcji bezpieczeństwa.

Zdarzenia informacyjne, np. zakończenie dozowania, wymiana butli czy synchronizacja RTC, pozostają eventami i nie wpływają na status, buzzer ani `active_alarm_count`.

## 40. Alarm vs diagnostyka

Jeśli informacja nie powinna wpływać na `alarm_active`, `status`, buzzer ani `active_alarm_count`, nie jest alarmem. Powinna trafić do diagnostyki, logów lub eventów.

Nie istnieje `service_only` ani ukryty alarm.

## 41. Brak Wi-Fi / MQTT

Sam brak Wi-Fi lub MQTT nie generuje alarmu. Są to stany diagnostyczne.

Alarm sieciowy może istnieć tylko wtedy, gdy konkretna funkcja domenowa naprawdę wymaga sieci do poprawnego lub bezpiecznego działania.

## 42. Storage/NVS

Problem storage/NVS staje się alarmem tylko wtedy, gdy wpływa na poprawne lub bezpieczne działanie.

Domyślnie:

```text
ERROR
```

Jeśli może spowodować niebezpieczne działanie:

```text
CRITICAL
```

i wymusza safe state.

## 43. Sensor fault

Utrata lub awaria ważnego sensora używanego do sterowania urządzeniem generuje co najmniej `ERROR`.

Jeśli bez sensora dalsza praca może być niebezpieczna:

```text
CRITICAL
```

## 44. Invalid sensor value

Jeśli sensor staje się `invalid`, alarmy progowe zależne od jego wartości nie są oceniane na podstawie starej wartości.

Stan sensora obsługuje osobny alarm typu `sensor_fault` lub odpowiednik domenowy.

## 45. delay_on

Każdy alarm może mieć opcjonalne `delay_on`.

Domyślnie:

```text
0
```

Warunek musi utrzymywać się przez `delay_on`, zanim alarm stanie się aktywny.

Mechanizm jest nieblokujący i timestamp-based.

## 46. delay_off

Każdy alarm może mieć opcjonalne `delay_off`.

Domyślnie:

```text
0
```

Warunek musi pozostawać nieaktywny przez `delay_off`, zanim alarm zostanie skasowany.

## 47. Histereza

Alarmy progowe mogą mieć opcjonalną histerezę, np.:

```text
activate > 30.0
clear    < 29.0
```

Domyślnie histereza = 0.

Kolejność:

```text
próg + histereza
-> delay_on / delay_off
-> aktywacja / clear
```

## 48. Brak minimalnego czasu aktywności

Nie wprowadzamy ogólnego `minimum_active_time`.

Stabilizację zapewniają histereza, `delay_on` i `delay_off`.

## 49. startup_grace_period

Alarm może mieć `startup_grace_period`. W tym czasie brak poprawnego odczytu / inicjalizacji nie aktywuje alarmu.

Grace period:
- jest per sensor/moduł,
- nie jest globalny,
- nie blokuje reszty systemu.

## 50. Startup i status

`startup_grace_period` nie generuje sztucznego WARNING/ERROR. Stan „jeszcze się inicjalizuje” należy do boot state / diagnostyki.

## 51. Ocena alarmów po restarcie

Po restarcie:
1. inicjalizują się wymagane moduły,
2. respektowane są grace periods,
3. alarmy są oceniane z aktualnego stanu,
4. dopiero później urządzenie może zostać uznane za gotowe/online.

Nie publikujemy chwilowego fałszywego `OK`.

## 52. ACK po restarcie

Zwykłe alarmy po restarcie zaczynają jako:

```text
acknowledged = false
```

ACK zwykłego alarmu nie jest zapisywany do NVS.

Alarm `latched`, który przetrwa restart, również wraca jako `acknowledged = false`.

## 53. Suppression w SERVICE

Pojedynczy alarm może mieć:

```text
suppressed_in_service = true
```

Core nie wycisza automatycznie wszystkich alarmów w SERVICE.

## 54. Zachowanie alarmu suppressed

Alarm stłumiony w SERVICE:
- nie jest liczony jako aktywny,
- nie wpływa na `alarm_active`,
- nie wpływa na `active_alarm_count`,
- nie wpływa na `status`,
- nie uruchamia buzzera.

Po wyjściu z SERVICE, jeśli przyczyna nadal istnieje, alarm natychmiast zaczyna normalnie obowiązywać.

## 55. Suppression a latched

Tłumienie przez tryb nie kasuje historii ani trwałego latcha.

## 56. Złożone warunki alarmu

Core nie implementuje silnika reguł `AND/OR/NOT`.

Projekt domenowy może używać dowolnej logiki i przekazuje do Core prosty wynik:

```text
condition_active = true/false
```

## 57. Zależności od trybów domenowych

Core zna wspólne `NORMAL/SERVICE`. Bardziej złożone zależności od trybów projektu są oceniane domenowo.

## 58. Rejestr alarmów

Alarmy są rejestrowane statycznie przy starcie. Core nie tworzy dynamicznych alarmów w runtime.

## 59. Lekki stan runtime

Dla każdego alarmu Core przechowuje tylko niezbędne dane, np.:

```text
active
acknowledged
condition_active
latched_active
suppressed
timestamps delay_on/off
activation timestamp
```

## 60. Wspólne encje HA

Domyślny zestaw:

```text
binary_sensor.alarm_active
sensor.active_alarm
sensor.active_alarm_count
sensor.alarm_severity
binary_sensor.safety_lock
switch.buzzer_enabled
button.ack_all
```

Dodatkowo istniejący `status`.

## 61. alarm_active

`ON` oznacza co najmniej jeden aktywny alarm, `OFF` oznacza brak aktywnych alarmów.

## 62. active_alarm

Publikuje `alarm_id` najważniejszego aktywnego alarmu.

Wybór:
1. najwyższy severity,
2. najwyższy priorytet,
3. kolejność rejestracji.

## 63. active_alarm_count

Publikuje liczbę aktywnych alarmów jako integer.

## 64. alarm_severity

Publikuje:

```text
NONE
WARNING
ERROR
CRITICAL
```

Wyliczane automatycznie przez Core.

## 65. safety_lock w HA

`safety_lock` jest `binary_sensor`.

## 66. buzzer_enabled w HA

`buzzer_enabled` jest `switch` i steruje dokładnie tą samą trwałą wartością co WWW.

## 67. ack_all w HA

`ack_all` jest `button`, a payload zgodnie z MQTT_STANDARD to:

```text
PRESS
```

## 68. Brak osobnej encji dla każdego alarmu

Core domyślnie nie tworzy osobnego `binary_sensor` dla każdego alarmu.

Projekt domenowy może świadomie wystawić wybrany alarm jako osobną encję, jeśli ma to realny sens.

## 69. MQTT eventy alarmowe

Zmiany alarmów generują wspólne eventy:

```text
alarm_activated
alarm_cleared
alarm_acknowledged
```

Są QoS 1, non-retained i nie są buforowane offline.

## 70. Payload eventów alarmowych

Event powinien pozostać lekki. Minimalnie zawiera `alarm_id`.

Mały JSON jest dopuszczalny tylko wtedy, gdy naprawdę potrzebne są dodatkowe dane.

## 71. Logowanie

Każda aktywacja, clear i ACK są automatycznie logowane przez wspólny logger Core.

## 72. Mapowanie na logger

Rekomendowane mapowanie:

```text
WARNING  -> WARN
ERROR    -> ERROR
CRITICAL -> ERROR
```

Pełne severity alarmu pozostaje częścią danych alarmu / wpisu. Nie ma potrzeby dodawania osobnego poziomu logowania `CRITICAL`.

ACK i clear mogą być logowane informacyjnie zgodnie z późniejszym DIAGNOSTICS_STANDARD.

## 73. Historia alarmów w WWW

Urządzenie przechowuje krótką historię runtime:

```text
20 ostatnich zdarzeń alarmowych
```

Jako lekki ring buffer.

Każdy wpis zawiera co najmniej:

```text
timestamp
event_type
alarm_id
```

## 74. Trwałość historii

Domyślnie historia znajduje się w RAM i nie musi przetrwać restartu.

Ewentualna trwałość może zostać zdefiniowana później w `DIAGNOSTICS_STANDARD` / `CONFIG_STORAGE_STANDARD`.

## 75. WWW — widok alarmów

WWW ma dwie główne sekcje:

```text
Aktywne alarmy
Ostatnie zdarzenia
```

## 76. WWW — aktywne alarmy

Każdy wpis może pokazywać:

```text
severity
alarm_id
description
recommended_action
category
ACK state
linked source value
```

Dla `latched` warto pokazać `condition_active` i `latched_active`.

## 77. WWW — safety_lock

Przy aktywnym `safety_lock` WWW pokazuje dokładny alarm / listę alarmów `CRITICAL`, które blokują wznowienie.

## 78. WWW — ostatnie zdarzenia

Wystarczy lekki widok:

```text
czas
typ zdarzenia
alarm_id
```

## 79. Brak wyłączania pojedynczych alarmów

Użytkownik nie może ręcznie wyłączyć konkretnego alarmu.

Może:
- globalnie wyłączyć dźwięki,
- korzystać z suppression wynikającego z logiki projektu,
- używać SERVICE tam, gdzie alarm jawnie to wspiera.

## 80. Brak ukrytych alarmów

Nie ma `service_only` ani `hidden_alarm`.

Jeśli dana informacja nie wpływa na system alarmowy, powinna być diagnostyką lub eventem.

## 81. Brak osobnego requires_ack

Nie istnieje parametr `requires_ack`.

Zasady ACK wynikają z typu alarmu: zwykły, latched, CRITICAL.

## 82. Status i alarmy podczas safe state

Safe state nie zatrzymuje alarm managera.

Podczas `safety_lock` nadal:
- wykrywamy nowe alarmy,
- aktualizujemy stare,
- pokazujemy diagnostykę,
- logujemy eventy.

## 83. Kilka CRITICAL jednocześnie

Jeśli aktywnych jest kilka `CRITICAL`:
- wszystkie pozostają widoczne,
- `active_alarm_count` liczy wszystkie,
- `active_alarm` pokazuje najważniejszy,
- safety actions się sumują,
- `safety_lock` pozostaje aktywny do rozwiązania wszystkich blokad.

## 84. Wymagania implementacyjne dla Core

Docelowo Core powinien dostarczyć co najmniej mechanizmy odpowiadające za:

```text
AlarmSeverity
AlarmCategory
AlarmDefinition
AlarmRuntimeState
AlarmManager
BuzzerService / AlarmBuzzerAdapter
AlarmHistoryRingBuffer
Alarm HA/MQTT adapter
```

Nazwy klas mogą się zmienić, ale podział odpowiedzialności powinien pozostać.

## 85. AlarmDefinition

Przykładowo zawiera:

```text
alarm_id
severity
category
priority
audible
latched
suppressed_in_service
delay_on
delay_off
hysteresis
startup_grace_period
description
recommended_action
source_entity_key
```

Definicja jest statyczna.

## 86. AlarmManager

Odpowiada za:
- delay_on,
- delay_off,
- latch,
- ACK,
- ACK ALL,
- suppression,
- najwyższe severity,
- alarm_active,
- active_alarm,
- active_alarm_count,
- alarm_severity,
- safety_lock,
- eventy,
- historię,
- logowanie.

Nie zna szczegółowej logiki procesowej urządzenia.

## 87. Domena projektu

Projekt domenowy odpowiada za:
- obliczenie `condition_active`,
- dobór severity,
- dobór audible,
- dobór latch dla WARNING/ERROR,
- safe state action,
- warunek bezpiecznego wznowienia,
- listę komend blokowanych przez safety_lock,
- powiązanie alarmu z sensorem/stanem.

## 88. Wydajność

Implementacja ma być lekka dla ESP:
- brak dynamicznego tworzenia alarmów,
- brak pełnych historii w RAM,
- brak dużych JSON-ów,
- brak blokujących `delay()`,
- brak duplikacji danych,
- brak osobnych HA encji dla każdego alarmu domyślnie,
- statyczne teksty tam, gdzie to możliwe,
- ring buffer historii,
- minimalne zapisy do flash.

## 89. Integracja z MQTT_STANDARD

Alarmy respektują wszystkie zasady `MQTT_STANDARD.md`, w szczególności:
- QoS 1,
- retained tylko dla state,
- eventy non-retained,
- brak kolejkowania offline,
- command jako żądanie,
- actual state jako źródło prawdy,
- stabilne unique_id,
- wspólny availability,
- pełna synchronizacja po reconnect.

## 90. Integracja z architekturą urządzenia

System alarmowy współpracuje z [ARCHITECTURE.md](ARCHITECTURE.md),
[SAFETY_STANDARD.md](SAFETY_STANDARD.md) i kontraktem domenowym urządzenia:
- restart urządzenia -> `mode = NORMAL`,
- fizyczny krótki klik -> ACK ALL + NORMAL,
- SERVICE może tłumić tylko jawnie wskazane alarmy,
- safety logic jest lokalna,
- działanie nie zależy od HA.

## 91. Reguła końcowa

System alarmowy nie jest tylko warstwą powiadomień.

Jest częścią lokalnego mechanizmu bezpieczeństwa urządzenia.

Home Assistant i WWW są interfejsami do obserwacji, ACK i konfiguracji buzzera, ale nie są wymagane do wykrywania alarmu, safe state, safety_lock ani działania zabezpieczeń.
