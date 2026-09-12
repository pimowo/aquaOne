# NAMING_VERSIONING_STANDARD.md

**Status:** DRAFT
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard nazewnictwa i wersjonowania dla całego ekosystemu **aquaOne**.

Dotyczy wszystkich projektów i bibliotek, m.in.:

- `aquaOneCore`
- `aquaOneLuma`
- `aquaOneDoser`
- `aquaOneHydro`
- `aquaOneClima`
- `aquaOneGas`
- `aquaOneFauna`

Standard określa:

- nazwy projektów i typów urządzeń,
- nazwy techniczne i identyfikatory,
- nazwy klas, plików i modułów,
- nazwy konfiguracji, stanów, komend i eventów,
- wersjonowanie firmware,
- wersjonowanie `aquaOneCore`,
- wersjonowanie config schema,
- wersjonowanie MQTT protocol,
- identyfikację buildów,
- zasady kompatybilności.

## Stan konwencji

### Implementation: CURRENT

Publiczne nazwy już używane w kodzie, storage, Web i MQTT są kontraktami kompatybilności.
Każdy projekt zachowuje obecne nazwy klas i rozszerzenia plików, dopóki osobny refactor nie
ma uzasadnienia i planu migracji. Wersje firmware, Core, schema i protokołu są niezależnymi
pojęciami.

### Implementation: TARGET

Nowe publiczne identyfikatory MUSZĄ stosować reguły tego dokumentu. Ujednolicenie historycznych
nazw jest TARGET i nie uzasadnia masowej zmiany istniejących klas lub plików. Ten dokument
jest źródłem prawdy dla nazw projektów, `device_type`, firmware/Core/protocol/schema version
oraz nazw kompatybilności.

---

# 2. Zasada nadrzędna

Nazewnictwo ma być:

- stabilne,
- techniczne,
- przewidywalne,
- jednoznaczne,
- wspólne dla całego ekosystemu.

Wersjonowanie ma pozwalać szybko odpowiedzieć na pytania:

```text
Jaki firmware działa?
Z jaką wersją Core został zbudowany?
Jaki config schema obsługuje?
Jaki protokół MQTT publikuje?
Z jakiego buildu pochodzi?
Czy ta konfiguracja / firmware / urządzenie są kompatybilne?
```

---

# 3. Nazwa ekosystemu

Oficjalna nazwa techniczna:

```text
aquaOne
```

Nie używamy równolegle wariantów:

```text
AquaOne
AquaONE
AQmaOne
aqua-one
```

w nazwach repozytoriów, namespace i identyfikatorach technicznych, chyba że wymaga tego konkretny kontekst UI.

---

# 4. Nazwy projektów

Oficjalne nazwy repozytoriów/projektów:

```text
aquaOneCore
aquaOneLuma
aquaOneDoser
aquaOneHydro
aquaOneClima
aquaOneGas
aquaOneFauna
```

Nowe projekty powinny stosować wzorzec:

```text
aquaOne<Name>
```

gdzie `<Name>` jest krótką nazwą domeny pisaną PascalCase.

---

# 5. device_type

Każdy projekt urządzenia posiada stabilny techniczny `device_type`.

Wspólny zestaw:

```text
luma
doser
hydro
clima
gas
fauna
```

`device_type` jest:

- małymi literami,
- bez spacji,
- bez wersji,
- bez friendly name,
- stabilny w czasie.

---

# 6. device_type nie jest nazwą użytkownika

`device_type` nie służy do personalizacji.

Nie:

```text
akwarium_salon
lampa_duza
doser_piotr
```

Tylko:

```text
luma
doser
hydro
```

Friendly name pozostaje po stronie UI/HA.

---

# 7. Nazwa techniczna urządzenia

Zgodnie z `MQTT_STANDARD` techniczna nazwa instancji:

```text
aquaone-<type>-<MAC6>
```

Przykład:

```text
aquaone-luma-A1B2C3
aquaone-doser-7F21AC
```

`MAC6` to ostatnie 6 znaków MAC zapisane wielkimi literami bez separatorów.

---

# 8. MQTT root

MQTT root:

```text
aquaone-<MAC6>/<type>
```

Przykład:

```text
aquaone-A1B2C3/luma
```

Nie zawiera friendly name.

---

# 9. MQTT client_id

MQTT `client_id`:

```text
aquaone-<type>-<MAC6>
```

Ma być stabilny i jednoznaczny.

---

# 10. Home Assistant unique_id

Wspólny wzorzec:

```text
aquaone_<type>_<MAC6>_<entity>
```

Przykład:

```text
aquaone_luma_A1B2C3_status
aquaone_doser_7F21AC_mode
```

`unique_id` pozostaje stabilny po zmianie friendly name w HA.

---

# 11. Techniczne identyfikatory

Dla:

- state,
- command,
- event,
- config key,
- alarm_id,
- source_entity_key,

używamy:

```text
snake_case
```

Przykłady:

```text
wifi_rssi
active_alarm_count
buzzer_enabled
water_level_low
dose_completed
```

---

# 12. Kody błędów i reason codes

Dla stabilnych kodów diagnostycznych używamy:

```text
UPPER_SNAKE_CASE
```

Przykłady:

```text
MQTT_AUTH_FAILED
SENSOR_TIMEOUT
SAFETY_LOCK
CONFIG_INVALID
```

---

# 13. Enumy transportowe

Wartości enum publikowane do MQTT/API, jeśli są częścią wspólnego kontraktu, zapisujemy wielkimi literami:

```text
NORMAL
SERVICE
OK
WARNING
ERROR
CRITICAL
ONLINE
DISCONNECTED
```

---

# 14. Boolean

W MQTT:

```text
ON
OFF
```

W kodzie:

```cpp
bool
```

W JSON API:

```json
true
false
```

Nie mieszamy reprezentacji w jednym interfejsie.

---

# 15. Jednostki w nazwach

Nie dodajemy jednostki do nazwy klucza, jeśli metadane lub kontekst jednoznacznie ją określają.

Preferowane:

```text
temperature
pressure
wifi_rssi
```

Nie:

```text
temperature_c
pressure_bar
wifi_rssi_dbm
```

Wyjątek: gdy dwie wartości tej samej wielkości mają różne jednostki i brak jednostki byłby niejednoznaczny.

---

# 16. Nazwy modułów Core

Nazwy modułów powinny być rzeczownikowe i jednoznaczne.

Przykłady:

```text
Config
Diagnostics
Logging
Network
System
Time
Web
Mqtt
Alarm
Safety
Ota
```

Nie tworzymy kilku modułów o podobnym znaczeniu bez wyraźnego powodu.

---

# 17. Nazwy klas

Klasy C++:

```text
PascalCase
```

Przykłady:

```text
ConfigManager
AlarmManager
MqttService
DiagnosticsManager
SafetyManager
```

---

# 18. Nazwy metod i zmiennych

Metody i zmienne:

```text
camelCase
```

Przykłady:

```text
loadConfig()
publishState()
isReady
lastError
```

---

# 19. Stałe

Stałe compile-time:

```text
UPPER_SNAKE_CASE
```

Przykład:

```text
MAX_RECONNECT_DELAY_MS
DEFAULT_MQTT_PORT
```

---

# 20. Nazwy plików

Preferowany wzorzec dla plików klas:

```text
PascalCase.hpp
PascalCase.cpp
```

Przykład:

```text
AlarmManager.hpp
AlarmManager.cpp
```

Jeśli projekt konsekwentnie używa `.h`, może pozostać przy `.h`, ale Core powinien mieć jeden wspólny styl.

---

# 21. Foldery

Foldery modułów:

```text
PascalCase
```

lub małe litery tylko jeśli wynika to z istniejącej struktury build systemu.

Dla `aquaOneCore` preferujemy spójność z istniejącymi modułami.

---

# 22. Nazwy callbacków

Callbacki powinny mówić jasno, kiedy są wywoływane.

Przykłady:

```text
onConfigChanged
onAlarmActivated
onSafetyLockChanged
prepareForOta
```

Unikamy ogólnych:

```text
handler
callback1
process
```

bez kontekstu.

---

# 23. Nazwy komend

Komendy powinny być jednoznaczne i opisywać cel.

Preferowane:

```text
restart
ack_all
buzzer_enabled
mode
start
stop
```

Nie używamy `toggle` dla funkcji, gdzie stan docelowy ma znaczenie bezpieczeństwa.

---

# 24. Nazwy eventów

Event opisuje to, co już się wydarzyło.

Preferowane formy:

```text
alarm_activated
alarm_cleared
dose_completed
feeding_completed
config_saved
```

Nie nazywamy eventu jak komendy.

---

# 25. Stabilność nazw publicznych

Nazwa używana w:

- MQTT,
- HA unique_id,
- backup config,
- alarm_id,
- API,

staje się częścią kontraktu.

Nie zmieniamy jej tylko dlatego, że nowa nazwa „brzmi lepiej”.

Zmiana wymaga migracji lub świadomego breaking change.

---

# 26. Firmware version

Każdy projekt urządzenia posiada:

```text
firmware_version
```

Format:

```text
MAJOR.MINOR.PATCH
```

zgodny z SemVer.

Przykład:

```text
1.4.2
```

---

# 27. Znaczenie MAJOR

Zwiększamy `MAJOR`, gdy występuje istotna niekompatybilna zmiana w zachowaniu lub publicznym kontrakcie.

Przykłady:

- niekompatybilny MQTT protocol,
- usunięcie istotnej funkcji,
- niekompatybilny format danych,
- znacząca zmiana API wymagająca migracji klienta.

---

# 28. Znaczenie MINOR

Zwiększamy `MINOR`, gdy dodajemy funkcje w sposób kompatybilny.

Przykład:

```text
1.3.0 -> 1.4.0
```

dla nowej funkcji bez łamania istniejącego kontraktu.

---

# 29. Znaczenie PATCH

Zwiększamy `PATCH` dla:

- bugfixów,
- poprawy stabilności,
- drobnych zmian implementacyjnych,
- zmian bez nowej funkcji publicznej.

---

# 30. Pre-release

Dopuszczalne:

```text
1.4.0-alpha.1
1.4.0-beta.2
1.4.0-rc.1
```

dla wersji testowych.

---

# 31. Production release

Wersja produkcyjna nie powinna zawierać suffixu pre-release.

Przykład:

```text
1.4.0
```

---

# 32. core_version

`aquaOneCore` posiada niezależne:

```text
core_version
```

również w SemVer:

```text
MAJOR.MINOR.PATCH
```

---

# 33. Firmware zawiera wersję Core

Każdy build projektu domenowego zapisuje wersję Core, z którą został zbudowany.

Przykład:

```text
firmware_version = 1.4.2
core_version = 0.8.0
```

---

# 34. Core nie jest aktualizowany osobno runtime

`aquaOneCore` jest linkowany/buildowany razem z firmware projektu.

Zmiana Core wymaga nowego firmware urządzenia.

---

# 35. Core MAJOR

`core_version MAJOR` zwiększamy, gdy API biblioteki lub zachowanie wspólnych modułów wymaga zmian w projektach korzystających z Core.

---

# 36. config_schema_version

Schemat trwałej konfiguracji ma osobną liczbę:

```text
config_schema_version
```

Przykład:

```text
1
2
3
```

Nie używamy SemVer dla schema.

---

# 37. Zmiana config schema

`config_schema_version` zwiększamy tylko wtedy, gdy zmienia się trwała struktura danych wymagająca migracji.

Nie zwiększamy jej dla każdej nowej wersji firmware.

---

# 38. mqtt_protocol_version

Wspólny kontrakt MQTT posiada:

```text
mqtt_protocol_version
```

Początkowa wersja:

```text
1
```

To osobna liczba całkowita.

---

# 39. Kiedy zwiększamy mqtt_protocol_version

Zwiększamy tylko przy niekompatybilnej zmianie wspólnego kontraktu MQTT.

Nie zwiększamy dla dodania domenowego topicu, jeśli istniejący kontrakt pozostaje kompatybilny.

---

# 40. Hardware revision

Jeśli projekt ma różne wersje PCB, używa:

```text
hardware_revision
```

Preferowany format:

```text
1
2
3
```

lub stabilny identyfikator typu:

```text
REV_A
REV_B
```

W obrębie jednego projektu wybieramy jeden styl i go utrzymujemy.

---

# 41. Hardware revision nie jest firmware version

Rozdzielamy:

```text
hardware_revision
firmware_version
core_version
```

Każda wartość opisuje coś innego.

---

# 42. build_id

Każdy build może posiadać:

```text
build_id
```

Rekomendowany format:

```text
<git-short-sha>
```

Przykład:

```text
a13f7c2
```

---

# 43. Dirty build

Jeśli build powstał z lokalnymi niezacommitowanymi zmianami, warto oznaczyć:

```text
build_dirty = true
```

lub suffix:

```text
a13f7c2-dirty
```

Wersje produkcyjne najlepiej budować z czystego repozytorium.

---

# 44. build_timestamp

Opcjonalnie:

```text
build_timestamp
```

w formacie ISO 8601 / UTC.

Przykład:

```text
2026-09-12T15:20:00Z
```

Nie jest źródłem wersji — tylko informacją pomocniczą.

---

# 45. Git tag

Wersja firmware release powinna odpowiadać tagowi Git.

Rekomendowany tag:

```text
v1.4.2
```

---

# 46. Tagi Core

Dla `aquaOneCore` również:

```text
v0.8.0
v1.0.0
```

---

# 47. Repozytoria domenowe i Core

W release notes projektu urządzenia warto zapisać:

```text
firmware_version
core_version
config_schema_version
mqtt_protocol_version
hardware compatibility
```

---

# 48. Compatibility metadata

Firmware może deklarować:

```text
device_type
supported_hardware
config_schema_min
config_schema_target
mqtt_protocol_version
```

Wykorzystuje to m.in. `OTA_STANDARD`.

---

# 49. Firmware compatibility z hardware

Firmware nie powinien uruchamiać niebezpiecznego sterowania na nieobsługiwanej rewizji sprzętu.

Jeśli hardware jest rozpoznawalny, należy go zweryfikować.

---

# 50. Core compatibility

Projekt powinien jawnie określać wersję / zakres wersji Core użyty do builda.

Preferowane jest przypięcie konkretnej wersji podczas stabilnych wydań.

---

# 51. Development dependency

Podczas aktywnego development można używać gałęzi roboczej Core, ale produkcyjny release powinien być odtwarzalny.

---

# 52. Reproducible build

Wersja release powinna pozwolić możliwie dokładnie odtworzyć build na podstawie:

- git tag/commit projektu,
- core version/commit,
- build environment,
- platform/framework version.

---

# 53. Platform/framework versions

Dla debugowania warto zapisywać w build metadata:

```text
framework_version
platform_version
```

Nie muszą być publikowane do HA.

---

# 54. Library versions

Nie publikujemy do HA pełnej listy bibliotek.

Może być dostępna w build manifest / diagnostics snapshot.

---

# 55. VERSION file

Każdy projekt może posiadać jedno źródło wersji, np.:

```text
VERSION
```

lub jeden nagłówek generowany podczas build.

Nie utrzymujemy kilku ręcznie aktualizowanych kopii wersji.

---

# 56. Single source of version truth

`firmware_version` musi mieć jedno źródło prawdy.

Z niego generujemy:

- UI,
- HA Discovery,
- diagnostics,
- OTA metadata,
- build info.

---

# 57. Core version source

`aquaOneCore` również ma jedno źródło `core_version`.

Projekt nie wpisuje jej ręcznie w kilku miejscach.

---

# 58. Compile-time metadata

Wersje i build metadata powinny być dostępne compile-time bez odczytu storage.

---

# 59. Wersja konfiguracji pochodzi ze storage

`config_schema_version` jest częścią trwałej struktury configu i jest również znana kodowi jako target schema.

---

# 60. Target config schema

Firmware zna:

```text
CONFIG_SCHEMA_TARGET
```

i porównuje ją z wersją zapisaną w storage.

---

# 61. Brak wersji w nazwach klas

Nie tworzymy klas typu:

```text
MqttManagerV2
ConfigManagerNew
AlarmManager2026
```

Wersjonowanie rozwiązujemy przez Git/API/schema, nie nazwę klasy.

---

# 62. Brak wersji w topicach bez potrzeby

Nie dodajemy:

```text
/v1/
```

do MQTT root w pierwszej wersji, ponieważ mamy osobne `mqtt_protocol_version`.

---

# 63. Brak daty jako firmware version

Nie używamy daty jako głównej wersji firmware:

```text
2026.09.12
```

Data może być build metadata.

---

# 64. Changelog

Każdy projekt powinien prowadzić prosty changelog wydań.

Wpis release powinien podawać:

```text
Added
Changed
Fixed
Migration notes
Known issues
```

Nie musi być rozbudowany.

---

# 65. Breaking change

Każda breaking change powinna być jawnie oznaczona w release notes.

Nie ukrywamy breaking changes w zwykłym PATCH.

---

# 66. Migration notes

Jeśli zmienia się:

- config schema,
- hardware requirement,
- MQTT protocol,

release notes muszą to zaznaczyć.

---

# 67. API stability

Publiczne API Core powinno być możliwie stabilne.

Implementacyjne klasy prywatne mogą się zmieniać bez zmiany MAJOR, jeśli projekty domenowe ich nie używają.

---

# 68. Public vs internal API

Core powinien jasno rozdzielać:

```text
public API
internal implementation
```

Domena korzysta tylko z public API.

---

# 69. Deprecation

Jeśli publiczne API ma zostać usunięte, preferujemy etap:

```text
supported
-> deprecated
-> removed in next MAJOR
```

jeśli koszt utrzymania kompatybilności jest rozsądny.

---

# 70. Nazwy dokumentów standardów

Wspólne standardy w `aquaOne/docs/` zapisujemy jako:

```text
UPPER_SNAKE_CASE.md
```

Przykłady:

```text
MQTT_STANDARD.md
ALARM_STANDARD.md
WEB_STANDARD.md
CONFIG_STORAGE_STANDARD.md
DIAGNOSTICS_STANDARD.md
OTA_STANDARD.md
SAFETY_STANDARD.md
```

---

# 71. Nazwy dokumentów projektowych

Dokumentacja wewnątrz projektu może używać numerowanych nazw, jeśli projekt tego potrzebuje.

Nie musi to wpływać na wspólne standardy.

---

# 72. Nazewnictwo branchy

Rekomendowane:

```text
main
develop
feature/<name>
fix/<name>
release/<version>
```

Nie jest wymagane używanie wszystkich typów branchy.

Dla małych projektów wystarczy:

```text
main
feature/<name>
```

---

# 73. Commit messages

Preferujemy krótkie techniczne commity opisujące jedną logiczną zmianę.

Nie narzucamy ciężkiego Conventional Commits jako obowiązku.

Można stosować np.:

```text
Add alarm registry
Fix MQTT reconnect state
Update config migration v2->v3
```

---

# 74. Release branch opcjonalny

Małe projekty nie muszą utrzymywać długowiecznych release branchy.

Git tag na stabilnym commicie jest wystarczający.

---

# 75. Nazewnictwo feature flag

Feature flags:

```text
FEATURE_<NAME>
```

lub logiczne compile-time constants zgodne ze stylem projektu.

Nie używamy niejasnych:

```text
TEST1
NEW_MODE
TEMP_FIX
```

w kodzie produkcyjnym.

---

# 76. Nazewnictwo konfiguracji domenowej

Klucze domenowe pozostają neutralne technicznie.

Przykład Luma:

```text
channel_1_limit
profile_id
night_level
```

Nie kodujemy nazw użytkownika w kluczach trwałych.

---

# 77. Nazwy kanałów użytkownika

User-defined names są danymi konfiguracyjnymi.

Nie stają się:

- MQTT topic names,
- unique_id,
- storage keys.

Dzięki temu użytkownik może zmienić nazwę bez breaking change.

---

# 78. Lokalizacja

Techniczne identyfikatory są po angielsku.

UI może prezentować polskie etykiety.

Przykład:

```text
technical: safety_lock
UI: Blokada bezpieczeństwa
```

---

# 79. Jedna semantyka nazwy

Ten sam termin powinien znaczyć to samo we wszystkich projektach.

Przykład:

```text
status
mode
safety_lock
buzzer_enabled
firmware_version
core_version
```

Nie tworzymy synonimów:

```text
device_status
work_mode
safe_lock
sound_enable
fw_ver
```

dla tych samych wspólnych pojęć.

---

# 80. Common vocabulary

Oficjalne wspólne terminy:

```text
status
mode
state
command
event
alarm
warning
error
critical
safety_lock
availability
diagnostics
config
firmware
core
hardware
```

---

# 81. `state` vs `status`

`state` oznacza bieżącą wartość/stan konkretnej rzeczy.

`status` oznacza zagregowany globalny health urządzenia:

```text
OK
WARNING
ERROR
```

Nie używamy tych terminów zamiennie.

---

# 82. `mode` vs `state`

`mode` oznacza świadomy tryb działania, np.:

```text
NORMAL
SERVICE
```

Nie jest synonimem dowolnego stanu maszyny.

---

# 83. `event` vs `alarm`

`event` jest zdarzeniem.

`alarm` jest aktywnym lub zatrzaśniętym problemem.

Nie nazywamy informacji typu `dose_completed` alarmem.

---

# 84. `error` jako severity i kod

`ERROR` jako severity/status jest enumem.

Konkretny powód posiada osobny code:

```text
SENSOR_TIMEOUT
CONFIG_INVALID
```

---

# 85. Reserved common keys

Wspólne klucze Core powinny być zarezerwowane i nieużywane przez domenę do innego znaczenia.

Przykłady:

```text
status
mode
uptime
wifi_rssi
ip_address
firmware_version
core_version
mqtt_protocol_version
alarm_active
alarm_severity
safety_lock
buzzer_enabled
```

---

# 86. Konflikt nazwy

Jeśli projekt domenowy chce użyć klucza już zarezerwowanego przez Core, musi wybrać inną nazwę.

Nie nadpisujemy semantyki wspólnego klucza.

---

# 87. Max length identyfikatorów

Identyfikatory powinny być możliwie krótkie.

Rekomendacja:

```text
alarm_id / entity key <= 64 znaków
error code <= 64 znaków
```

Nie tworzymy bardzo długich opisowych kluczy.

---

# 88. Opisy nie są identyfikatorami

Długi tekst dla użytkownika należy do:

```text
description
recommended_action
UI label
```

Nie do `alarm_id` czy topicu.

---

# 89. Wersje w HA

Do HA publikujemy co najmniej:

```text
firmware_version
core_version
```

jako encje diagnostyczne.

`config_schema_version` i `mqtt_protocol_version` mogą być dostępne diagnostycznie, ale nie muszą być domyślnymi głównymi encjami.

---

# 90. Wersje w WWW

WWW Diagnostics/System pokazuje:

```text
firmware_version
core_version
config_schema_version
mqtt_protocol_version
hardware_revision
build_id
```

jeśli są dostępne.

---

# 91. Wersje w diagnostic snapshot

Snapshot diagnostyczny powinien zawierać wszystkie dostępne informacje wersyjne.

---

# 92. Wersje w backupie configu

Backup configu powinien zawierać co najmniej:

```text
device_type
config_schema_version
```

oraz może zawierać:

```text
firmware_version
core_version
hardware_revision
```

---

# 93. Wersje w OTA metadata

OTA powinno znać co najmniej:

```text
firmware_version
device_type
core_version
```

i tam, gdzie potrzebne:

```text
hardware compatibility
config schema compatibility
```

---

# 94. Compatibility rule

Nie oceniamy kompatybilności wyłącznie po numerze firmware.

Sprawdzamy właściwy kontrakt:

- hardware revision,
- config schema,
- MQTT protocol,
- Core API podczas builda.

---

# 95. Brak sztucznego bumpowania wszystkich wersji

Zmiana jednej warstwy nie wymusza zmiany wszystkich numerów.

Przykład:

bugfix domenowy może zmienić:

```text
firmware_version
```

bez zmiany:

```text
core_version
config_schema_version
mqtt_protocol_version
```

---

# 96. Przykład kompletnej identyfikacji

```text
device_type: luma
device_id: aquaone-luma-A1B2C3
firmware_version: 1.4.2
core_version: 0.8.0
config_schema_version: 3
mqtt_protocol_version: 1
hardware_revision: REV_A
build_id: a13f7c2
```

To jest wystarczający zestaw do większości przypadków diagnostyki i kompatybilności.

---

# 97. Core API — odpowiedzialność

`aquaOneCore` powinien dostarczać wspólne mechanizmy/stałe dla:

```text
device identification
core_version
common keys
common enums
build metadata access
version diagnostics
```

---

# 98. Domena — odpowiedzialność

Projekt domenowy definiuje:

```text
device_type
firmware_version
hardware_revision
domain-specific entity keys
domain-specific error codes
```

---

# 99. Review nazw przed publikacją

Przed wprowadzeniem nowego publicznego identyfikatora warto sprawdzić:

- czy nie istnieje już odpowiednik,
- czy nazwa jest jednoznaczna,
- czy będzie miała sens za rok,
- czy nie zawiera szczegółu implementacyjnego,
- czy jej zmiana później wymagałaby migracji.

---

# 100. Reguła końcowa

Nazwy publiczne traktujemy jak kontrakt, a wersje jak narzędzie do precyzyjnego opisu kompatybilności.

**Jedno pojęcie ma jedną nazwę, każda warstwa ma własną wersję i nie zmieniamy stabilnych identyfikatorów bez rzeczywistej potrzeby.**
