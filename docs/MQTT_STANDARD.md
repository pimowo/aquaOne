# MQTT_STANDARD.md

**Status:** ACTIVE
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard MQTT dla całego ekosystemu **aquaOne**.

Dotyczy wszystkich urządzeń opartych o `aquaOneCore`, m.in.:

- `aquaOneLuma`
- `aquaOneDoser`
- `aquaOneHydro`
- `aquaOneClima`
- `aquaOneGas`
- `aquaOneFauna`

Standard określa:

- strukturę topiców,
- identyfikację urządzeń,
- zasady publikacji i subskrypcji,
- QoS i retained messages,
- dostępność urządzenia,
- reconnect i synchronizację,
- HA MQTT Discovery,
- wspólne encje,
- walidację encji i komend,
- format payloadów,
- zachowanie przy błędach,
- relację między `aquaOneCore` i projektami domenowymi.

Nie definiuje szczegółowo:

- alarmów,
- diagnostyki,
- storage/NVS,
- OTA,
- konfiguracji WWW,
- factory reset,
- bezpieczeństwa domenowego.

Te elementy mają własne standardy.

Dokumenty powiązane:

- alarmy: [ALARM_STANDARD.md](ALARM_STANDARD.md);
- diagnostyka: [DIAGNOSTICS_STANDARD.md](DIAGNOSTICS_STANDARD.md);
- nazwy, identity i wersje: [NAMING_VERSIONING_STANDARD.md](NAMING_VERSIONING_STANDARD.md);
- bezpieczeństwo komend i autonomii: [SAFETY_STANDARD.md](SAFETY_STANDARD.md).

Akcje alarmowe HA/MQTT opisane w ALARM_STANDARD są **TARGET/FUTURE**. Nie należą do
pierwszego read-only kontraktu MQTT Dosera.

## 1.1. Status i zakres pierwszego wdrożenia

Dokument opisuje kontrakt docelowy. MQTT w AquaCore nie jest jeszcze zaimplementowane.
Pierwsze wdrożenie ma być małym, weryfikowalnym podzbiorem, a nie migracją 1:1 istniejącego
Dosera.

Decyzje obowiązujące dla pierwszego wdrożenia:

- istniejące 205 encji Discovery Dosera nie stanowi docelowego kontraktu i nie będzie
   automatycznie przenoszone do Core;
- pierwszy kontrakt HA Dosera jest read-only;
- encje HA `text` i `time` mają status **CAN WAIT**;
- implementacja Core MQTT nastąpi dopiero po osobnym spike'u T0;
- limit zwykłego payloadu pozostaje równy 1024 B.

Rozdziały opisujące komendy, `text`, `time`, `number`, `select` i `button` pozostają regułami
dla późniejszych etapów. Nie rozszerzają zakresu pierwszego wdrożenia.

---

# 2. Główne założenia

1. Urządzenie aquaOne działa autonomicznie także bez MQTT.
2. Brak MQTT nie może blokować logiki domenowej.
3. MQTT służy do:
   - publikacji stanów,
   - zdalnego sterowania,
   - integracji z Home Assistant,
   - publikacji krótkotrwałych eventów.
4. MQTT nie służy do:
   - pełnej konfiguracji technicznej urządzenia,
   - przechowywania historii pomiarów,
   - debugowania,
   - OTA,
   - backupu konfiguracji.
5. `aquaOneCore` dostarcza wspólną infrastrukturę MQTT.
6. Projekt domenowy deklaruje własne encje i logikę, ale nie implementuje od zera:
   - topiców,
   - QoS,
   - retain,
   - availability,
   - identyfikatorów,
   - HA Discovery.

---

# 3. Identyfikacja urządzenia

## 3.1. Id urządzenia

Każde urządzenie ma stały techniczny identyfikator:

```text
aquaone-XXXXXX
```

gdzie:

```text
XXXXXX
```

to ostatnie 6 znaków adresu MAC:

- uppercase,
- bez dwukropków.

Przykład:

```text
MAC: AA:BB:CC:A1:B2:C3
ID:  aquaone-A1B2C3
```

---

## 3.2. Typ urządzenia

Typ urządzenia jest zawsze lowercase:

```text
luma
doser
hydro
clima
gas
fauna
```

---

## 3.3. MQTT root

Root MQTT:

```text
aquaone-XXXXXX/<type>
```

Przykład:

```text
aquaone-A1B2C3/luma
```

Root:

- jest generowany automatycznie,
- nie może być zmieniany przez użytkownika,
- jest widoczny w WWW tylko do odczytu.

---

## 3.4. MQTT client_id

`client_id`:

```text
aquaone-<type>-<6_MAC>
```

Przykład:

```text
aquaone-luma-A1B2C3
```

Ten sam format jest używany jako techniczna nazwa urządzenia w HA.

---

# 4. Struktura topiców

Oficjalny protokół MQTT urządzenia używa wyłącznie:

```text
availability
state/...
command/...
event/...
```

Pełna struktura:

```text
aquaone-XXXXXX/<type>/availability
aquaone-XXXXXX/<type>/state/<key>
aquaone-XXXXXX/<type>/command/<key>
aquaone-XXXXXX/<type>/event/<key>
```

Nie ma standardowej gałęzi:

```text
debug/...
```

Debug i diagnostyka techniczna pozostają lokalnie w WWW/logach lub są definiowane przez osobny standard diagnostyczny.

---

# 5. Płaska struktura kluczy

Klucze domenowe są płaskie.

Poprawnie:

```text
state/pump_1_speed
state/channel_1_level
state/water_temperature
```

Niepoprawnie:

```text
state/pump/1/speed
state/channel/1/level
```

Każdy oficjalny topic kończy się jednym technicznym kluczem.

---

# 6. Zasady nazw kluczy

Klucz:

- lowercase,
- `snake_case`,
- maksymalnie 64 znaki,
- zaczyna się literą,
- może zawierać wyłącznie:
  - `a-z`
  - `0-9`
  - `_`
- nie może zawierać `__`,
- nie może kończyć się `_`.

Poprawne:

```text
wifi_rssi
pump_1_speed
co2_pressure
```

Niepoprawne:

```text
1_pump
WiFi_RSSI
pump-speed
pump__speed
pump_
```

Te same zasady dotyczą:

- `state`,
- `command`,
- `event`.

---

# 7. Zastrzeżone nazwy Core

Projekty domenowe nie mogą redefiniować wspólnych kluczy systemowych.

Zastrzeżone klucze:

```text
status
mode
uptime
wifi_rssi
ip_address
firmware_version
core_version
mqtt_protocol_version
```

Zastrzeżone prefiksy:

```text
mqtt_*
wifi_*
firmware_*
core_*
system_*
```

---

# 8. Availability

Topic:

```text
aquaone-XXXXXX/<type>/availability
```

Payloady:

```text
online
offline
```

Availability:

- jedno wspólne dla całego urządzenia,
- jest retained,
- QoS 1,
- dotyczy wszystkich encji HA,
- jest jedynym źródłem informacji o dostępności urządzenia.

Nie istnieje osobna encja:

```text
state/online
```

---

# 9. LWT

Urządzenie używa Last Will and Testament.

LWT:

```text
topic:
aquaone-XXXXXX/<type>/availability

payload:
offline

retain:
true

QoS:
1
```

Po poprawnym połączeniu i pełnej synchronizacji urządzenie publikuje:

```text
online
```

---

# 10. Znaczenie `online`

`online` oznacza:

> MQTT jest połączone, subskrypcje są aktywne, HA Discovery zostało opublikowane, aktualny pełny stan został zsynchronizowany z brokerem i broker potwierdził krytyczne publikacje QoS 1.

Samo zestawienie TCP/MQTT nie oznacza jeszcze `online`.

---

# 11. QoS i retain

## 11.1. State

```text
QoS: 1
retain: true
```

## 11.2. Command

```text
QoS: 1
retain: false
```

## 11.3. Event

```text
QoS: 1
retain: false
```

## 11.4. Availability

```text
QoS: 1
retain: true
```

Projekt domenowy nie ustawia QoS i retain ręcznie dla oficjalnie zarejestrowanych encji.

`aquaOneCore` narzuca te wartości automatycznie.

---

# 12. Publikacja state

Każda aktualna wartość ma osobny topic.

Przykłady:

```text
.../state/mode
.../state/wifi_rssi
.../state/firmware_version
```

Nie używamy jednego dużego JSON do całego stanu urządzenia.

JSON jest dozwolony tylko wtedy, gdy struktura rzeczywiście wymaga kilku logicznie związanych pól.

---

# 13. Snapshot po connect/reconnect

Po każdym udanym connect/reconnect urządzenie publikuje pełny aktualny snapshot `state/...`.

Nie polega wyłącznie na retained messages istniejących już na brokerze.

---

# 14. Kolejność synchronizacji

Po connect/reconnect obowiązuje kolejność:

1. subskrypcja własnego:
   ```text
   command/#
   ```
2. publikacja HA Discovery,
3. publikacja pełnego aktualnego `state/...`,
4. oczekiwanie na wymagane `PUBACK`,
5. publikacja:
   ```text
   availability = online
   ```
6. oczekiwanie na `PUBACK` dla `online`,
7. dopiero wtedy urządzenie uznaje MQTT za w pełni gotowe.

---

# 15. PUBACK podczas synchronizacji

Podczas krytycznej synchronizacji Core czeka na `PUBACK` dla:

- HA Discovery,
- pełnego snapshotu `state/...`,
- `availability=online`.

Timeout:

```text
5 s
```

Jeśli wymagany `PUBACK` nie nadejdzie:

- urządzenie nie przechodzi do `online`,
- połączenie jest zrywane,
- uruchamiany jest standardowy reconnect/backoff.

---

# 16. PUBACK podczas normalnej pracy

Po wejściu w `online` brak pojedynczego `PUBACK`:

- nie powoduje natychmiastowego zerwania sesji,
- nie uruchamia ręcznie reconnect,
- retransmisję obsługuje klient MQTT zgodnie z QoS 1.

---

# 17. Reconnect

MQTT reconnect działa w tle i nie blokuje urządzenia.

Sekwencja backoff:

```text
2 s
5 s
10 s
30 s
60 s
60 s
60 s
...
```

Po udanym połączeniu sekwencja wraca do początku.

---

# 18. Timeout próby połączenia

Pojedyncza próba MQTT ma timeout:

```text
5 s
```

Musi być obsługiwana bez blokowania logiki domenowej.

---

# 19. Keepalive

Standardowy MQTT keepalive:

```text
30 s
```

---

# 20. Clean session

Połączenie używa:

```text
clean session / clean start
```

Każdy reconnect odbudowuje:

- subskrypcje,
- HA Discovery,
- pełny snapshot stanu,
- availability.

---

# 21. Brak MQTT

Przy utracie MQTT:

- urządzenie działa dalej lokalnie,
- nie restartuje się tylko z powodu braku MQTT,
- logika domenowa nie jest zatrzymywana,
- reconnect odbywa się w tle.

---

# 22. Brak kolejkowania state offline

Podczas braku MQTT Core nie przechowuje historii zmian `state/...`.

Przykład:

```text
NORMAL -> SERVICE -> NORMAL
```

Po reconnect publikowany jest wyłącznie aktualny stan:

```text
NORMAL
```

---

# 23. Brak kolejkowania eventów offline

Eventy powstałe podczas braku MQTT:

- nie są buforowane,
- nie są odtwarzane po reconnect.

Jeżeli informacja musi przetrwać offline, powinna być reprezentowana jako trwały:

- state,
- alarm,
- inny stan domenowy.

---

# 24. Brak kolejkowania command

Polecenia:

- nie są odkładane na później,
- nie są wykonywane po reconnect,
- są wykonane albo odrzucone w chwili odbioru.

---

# 25. Aktywacja command dopiero po pełnej synchronizacji

Urządzenie subskrybuje `command/#` na początku synchronizacji, ale nie wykonuje komend przed zakończeniem pełnej synchronizacji.

Komendy są aktywowane dopiero po wejściu urządzenia w stan:

```text
online
```

---

# 26. Zakres subskrypcji

Każde urządzenie subskrybuje wyłącznie:

```text
aquaone-XXXXXX/<type>/command/#
```

Nie wolno używać:

```text
aquaone/#
```

ani wildcardów obejmujących inne urządzenia.

---

# 27. Command jako żądanie

`command/...` jest zawsze żądaniem.

Źródłem prawdy jest:

```text
state/...
```

Po prawidłowym wykonaniu komendy urządzenie publikuje rzeczywisty stan.

Home Assistant nie może zakładać sukcesu wyłącznie na podstawie wysłanego command.

---

# 28. Błędne command

Niepoprawny payload:

- jest odrzucany,
- nie jest poprawiany,
- nie jest zgadywany,
- nie powoduje częściowego wykonania,
- nie zmienia state,
- jest odnotowany diagnostycznie.

---

# 29. Komenda poprawna składniowo, ale niedozwolona

Jeśli komenda jest poprawna składniowo, lecz nie może zostać wykonana z powodu aktualnego stanu lub bezpieczeństwa:

- jest odrzucana,
- state pozostaje bez zmian,
- przyczyna trafia do diagnostyki/alarmu.

Nie istnieje standardowy:

```text
command_result
```

dla każdej komendy.

---

# 30. Komendy explicit-state

Komendy powinny ustawiać jawny stan docelowy.

Preferowane:

```text
ON
OFF
NORMAL
SERVICE
25.0
```

Unikamy:

```text
TOGGLE
```

ze względu na możliwe duplikaty przy QoS 1.

---

# 31. Komendy jednorazowe

Akcje bez trwałego stanu używają:

```text
PRESS
```

Przykłady:

```text
restart
feed_now
```

Takie akcje muszą być odporne na duplikaty QoS 1.

Core dostarcza mechanizm deduplikacji/ochrony, a projekt domenowy określa zasady bezpieczeństwa konkretnej akcji.

---

# 32. Payload boolean

Wartości logiczne:

```text
ON
OFF
```

Dotyczy m.in.:

- `switch`,
- `binary_sensor`.

---

# 33. Payload select / enum

Closed-set enums są uppercase.

Przykłady:

```text
NORMAL
SERVICE
OK
WARNING
ERROR
```

Komendy tekstowe są case-sensitive.

Dopuszczalny jest tylko dokładny payload zadeklarowany przez projekt.

Nie ma aliasów:

```text
normal
Normal
serv
```

są odrzucane.

---

# 34. Liczby

Payload liczbowy zawiera wyłącznie wartość.

Poprawnie:

```text
24.6
-61
12.47
```

Niepoprawnie:

```text
24.6 C
-61 dBm
12,47
```

Jednostka i semantyka są przekazywane przez HA Discovery.

Separator dziesiętny:

```text
.
```

---

# 35. Kanoniczny format liczbowy

Core publikuje możliwie krótki format liczbowy.

Przykłady:

```text
24.6
```

zamiast:

```text
24.600
```

oraz:

```text
24
```

zamiast:

```text
24.0
```

---

# 36. Precyzja liczb

Każda encja liczbowa deklaruje precyzję:

```text
0..3
```

maksymalnie 3 miejsca po przecinku.

Przykłady:

```text
temperature -> 1
ph          -> 2 lub 3
dose_ml     -> 2 lub 3
wifi_rssi   -> 0
```

---

# 37. Zaokrąglanie

Jeśli wartość ma większą precyzję niż zadeklarowana:

- Core stosuje normalne matematyczne zaokrąglenie,
- nie obcina cyfr.

Przykład:

```text
24.678
precision=1
```

publikacja:

```text
24.7
```

---

# 38. Porównywanie stanów liczbowych

Core porównuje wartości po zastosowaniu zadeklarowanej precyzji.

Przykład:

```text
precision = 1

24.61 -> 24.6
24.64 -> 24.6
```

Druga wartość nie powoduje publikacji.

Dopiero:

```text
24.66 -> 24.7
```

powoduje nową publikację.

---

# 39. Brak wartości sensora

Nie wolno publikować sztucznych sentinel values typu:

```text
0
-127
9999
```

Jeśli aktualna wartość staje się nieważna:

- Core czyści retained state pustym retained payloadem,
- błąd raportowany jest osobnym mechanizmem diagnostycznym/alarmowym.

---

# 40. min_publish_interval

Dla szybko zmieniających się sensorów projekt może opcjonalnie zadeklarować:

```text
min_publish_interval
```

Ważne:

- mechanizm jest opcjonalny,
- nie dotyczy krytycznych stanów, trybów i alarmów,
- ważne zmiany są publikowane natychmiast.

---

# 41. Zachowanie podczas min_publish_interval

Jeśli podczas interwału wartość zmieni się kilka razy:

- wartości pośrednie nie są kolejkowane,
- po upływie interwału publikowana jest tylko najnowsza wartość.

---

# 42. Brak globalnego max_publish_interval

Core nie wymusza okresowego publikowania niezmienionych wartości.

Retained state przechowuje aktualny stan.

Heartbeat sensora może istnieć jako świadomy wyjątek domenowy.

---

# 43. Maksymalny payload

Maksymalny zwykły payload MQTT:

```text
1024 B
```

Większe dane należy przesyłać innymi mechanizmami.

Pomiar obecnego generatora Discovery Dosera dla 205 encji dał payloady od 350 B do 493 B,
średnio 426,21 B. Największy payload pozostawia 531 B zapasu, dlatego nie ma podstaw do
zwiększenia limitu przed T0. Pomiar jest dowodem pojemności, nie uzasadnieniem migracji
pełnego zestawu encji.

---

# 44. Encje tekstowe

Obsługa komponentu HA `text` ma status **CAN WAIT** i nie należy do pierwszego wdrożenia.
To samo dotyczy komponentu HA `time`. Poniższe reguły obowiązują, gdy te typy zostaną
świadomie dodane w późniejszym etapie.

Domyślny maksymalny tekstowy payload:

```text
128 znaków
```

Projekt może zadeklarować krótszy limit.

Projekt może też zadeklarować dodatkową walidację:

- dozwolone znaki,
- wzorzec,
- własny walidator.

---

# 45. switch

HA `switch` zawsze używa:

```text
state:
ON / OFF

command:
ON / OFF
```

Inne payloady nie są dozwolone.

---

# 46. binary_sensor

HA `binary_sensor`:

- jest read-only,
- nie ma `command_topic`,
- publikuje wyłącznie:
  ```text
  ON
  OFF
  ```

Znaczenie wynika z:

```text
device_class
```

Jeśli fizyczny sygnał ma odwrotną logikę, projekt domenowy koryguje ją przed publikacją.

---

# 47. sensor

HA `sensor`:

- jest read-only,
- nie może posiadać `command_topic`.

---

# 48. number

Każda sterowalna encja `number` musi deklarować:

```text
min
max
step
```

Core:

- odrzuca wartość spoza zakresu,
- nie przycina automatycznie do min/max,
- odrzuca wartość niepasującą do `step`,
- nie zaokrągla command do kroku.

---

# 49. select

`select` przyjmuje wyłącznie dokładnie zadeklarowane opcje.

Brak:

- aliasów,
- automatycznej zmiany wielkości liter,
- częściowego dopasowania.

---

# 50. button

HA `button` zawsze używa:

```text
PRESS
```

Znaczenie wynika z `command_topic`.

Przykład:

```text
.../command/restart
payload: PRESS
```

---

# 51. Restart

Wspólna encja restart:

```text
component:
button

entity_category:
config

command:
.../command/restart

payload:
PRESS
```

Kontrolowany restart:

1. publish retained:
   ```text
   availability=offline
   ```
2. czekaj maksymalnie:
   ```text
   1 s
   ```
   na `PUBACK`,
3. wywołaj `disconnect()`,
4. nie czekaj dalej,
5. restart ESP.

Nagły zanik zasilania obsługuje LWT.

---

# 52. Mode

Każde urządzenie posiada:

```text
state/mode
command/mode
```

HA component:

```text
select
```

Dozwolone wartości:

```text
NORMAL
SERVICE
```

Po każdym restarcie urządzenia:

```text
mode = NORMAL
```

SERVICE nie jest odtwarzany po restarcie.

---

# 53. Status

Każde urządzenie publikuje:

```text
state/status
```

Dozwolone wartości:

```text
OK
WARNING
ERROR
```

`status` reprezentuje najwyższy aktualny poziom problemu/zdrowia urządzenia.

Jest to główna encja HA, nie diagnostyczna.

---

# 54. Wspólne obowiązkowe state

Wszystkie urządzenia używają identycznego kontraktu:

```text
state/status
state/mode
state/uptime
state/wifi_rssi
state/ip_address
state/firmware_version
state/core_version
state/mqtt_protocol_version
```

Projekt domenowy nie może zmieniać ich znaczenia ani nazwy.

---

# 55. uptime

Payload:

```text
integer seconds since last restart
```

Przykład:

```text
86400
```

ESP nie formatuje uptime do dni/godzin/minut.

Prezentacja należy do HA.

`uptime` ma:

```text
entity_category: diagnostic
```

---

# 56. wifi_rssi

Payload:

```text
raw dBm
```

Przykład:

```text
-61
```

Brak:

- procentów,
- opisów jakości,
- przeliczania na zakres 0–100.

HA Discovery odpowiada za jednostkę i semantykę.

`wifi_rssi`:

```text
entity_category: diagnostic
```

---

# 57. ip_address

Payload plain text:

```text
192.168.1.50
```

Encja:

- read-only,
- retained,
- diagnostic.

---

# 58. firmware_version

Format:

```text
MAJOR.MINOR.PATCH
```

Przykład:

```text
1.4.2
```

Bez:

```text
v1.4.2
2026-09-12
1.4.2-beta-extra-text
```

Encja:

```text
entity_category: diagnostic
```

---

# 59. core_version

Format:

```text
MAJOR.MINOR.PATCH
```

Przykład:

```text
1.2.0
```

Bez prefixu `v`.

Encja:

```text
entity_category: diagnostic
```

---

# 60. mqtt_protocol_version

Obowiązkowy retained state:

```text
state/mqtt_protocol_version
```

Początkowa wartość:

```text
1
```

Encja:

```text
entity_category: diagnostic
```

Wersję zwiększamy tylko przy zmianie niekompatybilnej wstecz, np.:

- wymaganej zmianie topicu,
- zmianie znaczenia payloadu,
- zmianie formatu command.

Dodanie nowego opcjonalnego topicu nie zwiększa wersji.

---

# 61. Brak device_type state

Nie publikujemy:

```text
state/device_type
```

Typ urządzenia już istnieje w ścieżce MQTT.

---

# 62. Brak device_name

Nie publikujemy:

```text
state/device_name
```

Core używa wyłącznie nazw technicznych.

Przyjazne nazwy użytkownik może ustawić w Home Assistant.

WWW również nie przechowuje osobnej przyjaznej nazwy urządzenia jako elementu standardu MQTT.

---

# 63. Brak mac_address state

Pełny MAC nie jest obowiązkową encją MQTT.

Pełny adres MAC może być dostępny lokalnie w diagnostyce WWW.

---

# 64. Brak mqtt_enabled state

Nie publikujemy:

```text
state/mqtt_enabled
```

Stan konfiguracji MQTT pozostaje lokalnie w WWW.

---

# 65. Brak mqtt_broker state

Nie publikujemy:

```text
state/mqtt_broker
```

Broker, port, username i password są konfiguracją lokalną WWW.

---

# 66. Brak boot_id

Nie publikujemy obowiązkowego:

```text
state/boot_id
```

Do standardowej obsługi wystarczają:

- availability,
- uptime,
- pełna synchronizacja po reconnect.

---

# 67. Brak last_seen

Nie publikujemy:

```text
state/last_seen
```

Nie używamy dodatkowego heartbeat timestamp.

---

# 68. reset_reason

`reset_reason` nie jest częścią obowiązkowego MQTT_STANDARD.

Jego ewentualna publikacja zostanie ustalona w:

```text
DIAGNOSTICS_STANDARD
```

---

# 69. time_status

`time_status` nie jest obowiązkowym elementem MQTT_STANDARD.

Zasady RTC/NTP zostaną zdefiniowane w osobnym standardzie diagnostyki/czasu.

---

# 70. hardware_revision

Może istnieć:

```text
state/hardware_revision
```

ale jego:

- format,
- obowiązkowość,
- semantyka

zostaną ustalone w:

```text
NAMING_VERSIONING_STANDARD
```

Jeśli istnieje, jest retained i diagnostic.

---

# 71. config_schema_version

Może istnieć:

```text
state/config_schema_version
```

ale jego zasady zostaną określone w:

```text
CONFIG_STORAGE_STANDARD
```

Jeśli istnieje, jest retained i diagnostic.

---

# 72. MQTT konfiguracja

Konfiguracja MQTT odbywa się lokalnie przez WWW.

Pola:

```text
MQTT enabled
broker
port
username
password
HA Discovery enabled
discovery_prefix
```

---

# 73. Broker

Domyślny broker:

```text
blank
```

Core nie zgaduje:

- hosta,
- IP,
- mDNS.

Jeżeli broker jest pusty, konfiguracja MQTT jest niekompletna.

---

# 74. Port

Domyślny port:

```text
1883
```

Port można zmienić w WWW.

---

# 75. Konto MQTT

Wszystkie urządzenia aquaOne mogą używać jednego wspólnego technicznego konta MQTT.

Przykład:

```text
username: aquaone
```

Identyfikacja urządzenia odbywa się przez:

```text
client_id
```

a nie przez osobne konto brokera dla każdego urządzenia.

---

# 76. Anonymous MQTT

Połączenia anonimowe są zabronione.

Wymagane:

```text
username
password
```

Bez poprawnych danych konfiguracja jest niekompletna.

---

# 77. Password w WWW

Hasło MQTT:

- jest zamaskowane domyślnie,
- nigdy nie jest zwracane przez Web API ani UI, także po uwierzytelnieniu,
- może używać pustego inputu w znaczeniu „bez zmiany”, jeśli endpoint jawnie definiuje tę
   semantykę,
- nie jest publikowane przez MQTT,
- nie jest pokazywane w logach/diagnostyce jawnie.

Szczegóły storage hasła definiuje `CONFIG_STORAGE_STANDARD`.

---

# 78. Test MQTT

WWW udostępnia funkcję:

```text
Test MQTT
```

Przed zapisaniem konfiguracji.

Test sprawdza:

- broker,
- port,
- username,
- password.

Przykładowe wyniki:

```text
connected
bad authentication
broker unavailable
timeout
```

---

# 79. Zastosowanie nowej konfiguracji MQTT

Po zapisaniu nowej konfiguracji MQTT:

- ESP nie restartuje się,
- stare połączenie MQTT jest zamykane,
- nowa konfiguracja jest aktywowana,
- następuje reconnect,
- wykonywana jest pełna standardowa synchronizacja.

---

# 80. Błędna nowa konfiguracja MQTT

Jeśli użytkownik zapisze błędny broker/hasło:

- konfiguracja pozostaje zapisana,
- brak automatycznego rollback do poprzedniej,
- urządzenie nadal działa autonomicznie,
- reconnect używa standardowego backoff,
- WWW pokazuje przyczynę problemu.

---

# 81. MQTT enabled

MQTT można całkowicie wyłączyć lokalnie w WWW.

Gdy MQTT jest wyłączone:

- urządzenie działa autonomicznie,
- nie wykonuje prób reconnect,
- nie publikuje state,
- nie publikuje Discovery.

Przed świadomym wyłączeniem MQTT:

1. publikowane jest retained:
   ```text
   availability=offline
   ```
2. Core czeka do 1 s na `PUBACK`,
3. wykonuje `disconnect()`.

---

# 82. MQTT enabled, ale konfiguracja niepełna

Jeśli:

```text
MQTT enabled = true
```

ale brakuje:

- brokera,
- username,
- password,

Core:

- nie wykonuje prób połączenia,
- nie traktuje tego jako alarmu,
- WWW pokazuje:
  ```text
  MQTT: not configured
  ```

---

# 83. TLS

TLS nie jest wymagany w pierwszej wersji standardu.

Podstawowy wariant:

```text
MQTT + username/password
trusted LAN
```

Architektura Core ma umożliwić dodanie TLS w przyszłości bez zmiany:

- topiców,
- identyfikatorów,
- payload contract.

---

# 84. HA MQTT Discovery

HA Discovery jest publikowane poza rootem urządzenia.

Standard:

```text
homeassistant/<component>/<unique_id>/config
```

Przykład:

```text
homeassistant/sensor/aquaone_luma_A1B2C3_wifi_rssi/config
```

---

# 85. Discovery prefix

Domyślny prefix:

```text
homeassistant
```

Można go zmienić w WWW.

Po zmianie prefixu Core:

1. usuwa retained Discovery spod starego prefixu,
2. publikuje pełne Discovery pod nowym prefixem,
3. robi to bez restartu ESP.

---

# 86. Włączenie i wyłączenie HA Discovery

WWW posiada:

```text
HA Discovery enabled
```

Domyślnie:

```text
enabled
```

Wyłączenie Discovery:

1. Core usuwa własne retained konfiguracje Discovery,
2. przestaje publikować Discovery,
3. MQTT state/command nadal działa.

Ponowne włączenie Discovery:

1. bez restartu ESP publikuje pełny zestaw Discovery,
2. ponownie publikuje pełny aktualny snapshot `state/...`.

---

# 87. Discovery retained

Wszystkie konfiguracje HA Discovery:

```text
retain: true
```

Są publikowane:

- przy starcie,
- po reconnect,
- po zmianie konfiguracji Discovery,
- po zmianie zestawu encji.

---

# 88. Grupowanie w HA

Wszystkie encje jednego fizycznego kontrolera należą do jednego HA device.

Identifier urządzenia:

```text
aquaone-XXXXXX
```

Techniczna nazwa urządzenia:

```text
aquaone-<type>-<6_MAC>
```

Przykład:

```text
aquaone-luma-A1B2C3
```

---

# 89. unique_id

Globalny wzór:

```text
aquaone_<type>_<6_MAC>_<entity>
```

Przykład:

```text
aquaone_luma_A1B2C3_wifi_rssi
```

`unique_id`:

- jest stabilny,
- nie zależy od friendly name,
- nie zależy od Discovery prefix.

---

# 90. object_id / suggested entity_id

Stosowany jest ten sam globalny techniczny wzór.

Przykład:

```text
sensor.aquaone_luma_A1B2C3_wifi_rssi
select.aquaone_luma_A1B2C3_mode
```

---

# 91. Friendly names

Core nie przechowuje własnych przyjaznych nazw użytkownika.

Discovery używa nazw technicznych.

Przykład:

```text
wifi_rssi
```

może być prezentowane jako:

```text
WiFi RSSI
```

Użytkownik może później zmienić nazwy w Home Assistant.

---

# 92. Metadata HA

Jeśli Home Assistant posiada standardowe pola, używamy ich zamiast własnych rozwiązań.

Dotyczy m.in.:

```text
device_class
state_class
unit_of_measurement
entity_category
```

Custom icon używamy tylko wtedy, gdy standardowa semantyka HA nie wystarcza.

---

# 93. entity_category

Wspólne encje:

```text
firmware_version -> diagnostic
core_version -> diagnostic
ip_address -> diagnostic
wifi_rssi -> diagnostic
uptime -> diagnostic
mqtt_protocol_version -> diagnostic
restart -> config
status -> main
mode -> main
```

---

# 94. Common availability w HA

Każda encja Discovery używa wspólnego:

```text
availability_topic
```

w tym:

- sensory,
- select,
- switch,
- button,
- diagnostyka.

Jeśli device jest offline, wszystkie encje są unavailable.

---

# 95. Rejestr encji Core

`aquaOneCore` udostępnia jedno wspólne API/rejestr encji MQTT/HA.

Projekt domenowy deklaruje:

- typ encji,
- klucz,
- właściwości,
- metadata,
- callback/logikę.

Core buduje:

- `state_topic`,
- `command_topic`,
- `availability_topic`,
- `unique_id`,
- `object_id`,
- Discovery config,
- QoS,
- retain.

Rejestr należy do warstwy neutralnej i nie zna pomp, profili ani harmonogramów. Core posiada
transport, lifecycle połączenia, topic builder, QoS/PUBACK, availability, synchronizację i
serializację Discovery. Firmware urządzenia dostarcza deskryptory encji, snapshot stanu oraz
walidowane callbacki domenowe.

---

# 96. Walidacja rejestru

Core waliduje deklaracje przy starcie.

Sprawdza m.in.:

- duplikaty kluczy,
- duplikaty `unique_id`,
- poprawność snake_case,
- limit długości,
- zgodność typu encji,
- kolizje przestrzeni nazw,
- dozwolone command/state combinations.

---

# 97. Błąd pojedynczej encji

Jeśli błędna jest tylko jedna encja:

- blokowana jest tylko ta encja,
- pozostałe encje działają normalnie.

---

# 98. Błąd wspólnej infrastruktury

Jeśli błąd dotyczy:

- technicznej tożsamości urządzenia,
- wspólnego rejestru,
- infrastruktury Core,

MQTT/HA może zostać zablokowane.

Urządzenie nadal musi działać autonomicznie lokalnie.

Core:

- nie restartuje ESP,
- nie blokuje logiki domenowej,
- zapisuje wyraźny błąd diagnostyczny,
- pokazuje go w WWW.

---

# 99. Oficjalne topiki tylko przez rejestr

Oficjalne:

```text
state
command
event
```

będące częścią publicznego kontraktu urządzenia muszą być zarejestrowane w Core.

Ręczna publikacja poza rejestrem może istnieć wyłącznie jako świadomy wyjątek techniczny/debugowy.

---

# 100. Command i state

Każda sterowalna encja z:

```text
command_topic
```

musi posiadać odpowiadający:

```text
state_topic
```

Wyjątek:

- akcje jednorazowe typu `button`.

---

# 101. Event

`event/...` służy wyłącznie do krótkotrwałych zdarzeń, które mają znaczenie poza samym state.

Przykład:

```text
event/feed_started
```

Event:

```text
QoS 1
retain false
```

Odbiorca musi tolerować duplikaty.

---

# 102. Event payload

Domyślnie event używa prostego payloadu tekstowego/liczbowego.

JSON tylko wtedy, gdy event rzeczywiście wymaga kilku powiązanych wartości.

Limit:

```text
1024 B
```

---

# 103. Publikacja stanu po zmianie źródła

Każda realna zmiana state musi zostać opublikowana niezależnie od źródła.

Źródłem może być:

- fizyczny przycisk,
- WWW,
- logika lokalna,
- MQTT command,
- automatyka urządzenia.

HA zawsze ma odzwierciedlać rzeczywisty stan urządzenia.

---

# 104. Ważne stany

Ważne:

- mode,
- alarmy,
- krytyczne stany,
- status urządzenia,

są publikowane przy każdej realnej zmianie bez debounce MQTT.

Szybka telemetria może używać:

- precision,
- min_publish_interval,
- progów domenowych.

---

# 105. Broker restart

Restart brokera i chwilowa utrata połączenia są traktowane identycznie jak normalny reconnect.

Nie istnieje osobna logika dla przyczyny rozłączenia.

---

# 106. Brak mqtt_status state

Nie publikujemy:

```text
state/mqtt_status
```

Availability reprezentuje dostępność MQTT.

Dokładne przyczyny błędów znajdują się w:

- WWW,
- logach,
- późniejszym DIAGNOSTICS_STANDARD.

---

# 107. Zmiany firmware i Discovery

## 107.1. Dodanie encji

Po aktualizacji firmware Core publikuje aktualny komplet Discovery.

Istniejące `unique_id` pozostają stabilne.

## 107.2. Usunięcie encji

Core usuwa:

- stary retained Discovery,
- stary retained `state/...`.

## 107.3. Zmiana klucza

Zmiana:

```text
water_temp
```

na:

```text
water_temperature
```

oznacza:

- usunięcie starej encji,
- usunięcie starego retained state,
- utworzenie nowej encji,
- nowe `unique_id`.

## 107.4. Zmiana metadanych

Zmiana np.:

- unit,
- device_class,
- state_class,
- icon,
- min/max,

przy zachowaniu tego samego klucza i znaczenia:

- zachowuje `unique_id`,
- nadpisuje retained Discovery.

## 107.5. Zmiana typu HA

Zmiana np.:

```text
sensor -> number
```

przy tym samym kluczu:

- usuwa stare Discovery spod starego komponentu,
- publikuje nowe Discovery,
- zachowuje ten sam `unique_id`.

---

# 108. Rejestr poprzedniego Discovery

Core trwale przechowuje listę wcześniej opublikowanych topiców Discovery.

Po zmianie firmware/zestawu encji:

1. porównuje starą listę z nową,
2. usuwa nieaktualne retained Discovery,
3. usuwa odpowiadające osierocone retained state,
4. publikuje nowy komplet,
5. po udanej synchronizacji zapisuje nową listę.

Szczegóły storage definiuje `CONFIG_STORAGE_STANDARD`.

---

# 109. MQTT protocol version i topic path

Wersja protokołu MQTT nie jest częścią ścieżki topicu.

Nie używamy:

```text
aquaone/v1/...
```

Używamy:

```text
aquaone-A1B2C3/luma/state/...
```

Wersja znajduje się osobno w:

```text
state/mqtt_protocol_version
```

---

# 110. Typy urządzeń i domena

Core nie zna logiki domenowej urządzeń.

Projekt domenowy może tworzyć własne:

```text
state/<key>
command/<key>
event/<key>
```

pod warunkiem zachowania tego standardu.

Docelowo Core zapewnia mechanizmy opisane w tym standardzie.

Projekt dostarcza znaczenie.

---

# 111. Brak sztywnego limitu liczby encji

MQTT_STANDARD nie narzuca globalnego limitu liczby encji na urządzenie.

Core pilnuje dostępnych zasobów.

Jeśli rejestr przekracza możliwości sprzętu:

- zgłasza błąd,
- nie dopuszcza do destabilizacji urządzenia.

---

# 112. Kontrolowane wyłączenie MQTT

Przy:

- restarcie,
- wyłączeniu MQTT w WWW,
- świadomym rozłączeniu,

Core wykonuje:

1. publish retained:
   ```text
   availability=offline
   ```
2. czeka maksymalnie:
   ```text
   1 s
   ```
   na `PUBACK`,
3. wykonuje:
   ```text
   disconnect()
   ```
4. kontynuuje restart/wyłączenie bez dalszego czekania.

---

# 113. Zasada nadrzędna

Najważniejsza zasada całego standardu:

> MQTT ma być przewidywalnym, spójnym i prostym interfejsem do aktualnego stanu oraz sterowania urządzeniem. Nie może przejmować odpowiedzialności za autonomiczne działanie urządzenia i nie może komplikować domenowej logiki projektu.

`aquaOneCore` odpowiada za infrastrukturę i spójność protokołu.

Projekt domenowy odpowiada za własną funkcjonalność i bezpieczeństwo swojej logiki.

---

# 114. Minimalny kontrakt HA Dosera

Pierwszy adapter Dosera publikuje wyłącznie stan. Nie subskrybuje `command/...` i nie tworzy
`command_topic` w Discovery.

Dla każdej z 8 pomp publikuje pięć encji:

```text
pump_X_enabled
pump_X_dose
pump_X_remaining
pump_X_next_dose
pump_X_low_liquid
```

Daje to dokładnie `5 × 8 = 40` encji per-pump.

Gdzie `X` jest numerem pompy:

- `pump_X_enabled` — encja read-only informująca, czy pompa jest aktywna w konfiguracji
   i harmonogramie;
- `pump_X_dose` — sensor read-only z aktualnie ustawioną dawką w ml;
- `pump_X_remaining` — sensor read-only z pozostałą ilością płynu;
- `pump_X_next_dose` — sensor/timestamp read-only z terminem następnego planowanego dozowania;
- `pump_X_low_liquid` — binary sensor read-only alarmujący o niskim poziomie płynu.

Ponadto publikuje jedną encję globalną:

```text
automatic_dosing
```

`automatic_dosing` jest encją read-only informującą, czy automatyczne dozowanie aktualnie
działa.

Minimalny kontrakt Dosera zawiera zatem dokładnie **41 encji domenowych: 40 per-pump + 1
globalną**. Wspólne encje Core i availability są liczone osobno.

Do tego dochodzą wspólne encje Core z rozdziału 54 i wspólne availability. Nazwa pompy,
edycja czasu, dni tygodnia, kalibracja, ustawianie poziomu zbiornika, ręczne dozowanie,
restart, per-pump `status` i pozostałe encje starego zestawu 205 są poza pierwszym kontraktem.
Pełna konfiguracja oraz szczegółowa diagnostyka pozostają dostępne przez WWW. Elementy spoza
minimalnego kontraktu mogą wrócić wyłącznie jako osobno uzasadnione rozszerzenia.

# 115. Wybór transportu i T0

Rekomendowanym kandydatem backendu ESP32 jest natywny **ESP-MQTT**, ukryty za neutralnym
interfejsem AquaCore. PubSubClient nie spełnia kontraktu, ponieważ publikacja wychodząca jest
QoS 0 i biblioteka nie udostępnia identyfikatora publikacji ani zdarzenia PUBACK.

Przed implementacją Core należy wykonać osobny spike **T0** na aktualnym toolchainie projektu.
T0 ma potwierdzić:

- kompilację i linkowanie ESP-MQTT z Arduino-ESP32 używanym przez projekty;
- nieblokujące połączenie i reconnect;
- publikację QoS 1 zwracającą `msg_id` oraz odbiór odpowiadającego PUBACK;
- LWT, clean session, keepalive i kontrolowane rozłączenie;
- poprawną obsługę payloadu 1024 B;
- koszt RAM/Flash na ESP32 i ESP32-S3.

T0 nie implementuje rejestru encji, HA Discovery ani adaptera Dosera. Negatywny wynik T0
powoduje ponowny wybór transportu bez zmiany neutralnego kontraktu Core.
