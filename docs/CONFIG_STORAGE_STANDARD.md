# CONFIG_STORAGE_STANDARD.md

**Status:** DRAFT
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard konfiguracji i trwałego przechowywania danych dla całego ekosystemu **aquaOne**.

Dotyczy wszystkich urządzeń korzystających z `aquaOneCore`, m.in.:

- `aquaOneLuma`
- `aquaOneDoser`
- `aquaOneHydro`
- `aquaOneClima`
- `aquaOneGas`
- `aquaOneFauna`

Standard określa:

- co zapisujemy trwale,
- czego nie zapisujemy,
- strukturę konfiguracji,
- wersjonowanie schematu,
- migracje konfiguracji,
- walidację,
- atomic save,
- factory reset,
- backup i restore,
- obsługę błędów storage,
- minimalizację zapisów flash,
- odpowiedzialność `aquaOneCore` i projektów domenowych.

## Stan implementacji

### Implementation: CURRENT

AquaCore 0.6.2 udostępnia `StorageBackend`, `PreferencesStorageBackend`, `StorageRecord`
i `StorageService`. Bieżący mechanizm zapewnia dwa sloty, numer generacji, CRC nagłówka i
payloadu, zgodność schematu, callback walidujący, wybór najnowszego poprawnego rekordu oraz
ponowny odczyt i weryfikację po zapisie.

### Implementation: REQUIRED NEXT

Każdy projekt używający storage MUSI dokumentować swoje rekordy, wersję schematu, walidator,
wartości domyślne i politykę obsługi uszkodzenia. Implementacja pełnego lifecycle CFG-101,
w tym odczytu stored schema niezależnie od `CURRENT`, pozostaje następnym krokiem.

### Implementation: TARGET

CFG-101 jest ACCEPTED — TARGET: Storage pozostaje mechanizmem raw record, a Config/Application
odpowiada za version/decode/migration/validation/persist/apply oraz jawny desired/active status.
Project-specific typed migrations pozostają po stronie adaptera projektu. Backup/import/export,
factory reset i koordynacja wielu rekordów nie są częścią obecnego API `StorageService`.

Nazwy urządzeń i wersji definiuje [NAMING_VERSIONING_STANDARD.md](NAMING_VERSIONING_STANDARD.md),
factory reset definiuje [FACTORY_RESET_ONBOARDING_STANDARD.md](FACTORY_RESET_ONBOARDING_STANDARD.md),
a wymagania bezpieczeństwa [SAFETY_STANDARD.md](SAFETY_STANDARD.md).

---

# 2. Zasada nadrzędna

Trwała konfiguracja ma być:

- spójna,
- walidowana,
- odporna na restart,
- odporna na częściowy zapis,
- łatwa do migracji,
- lekka dla flash,
- niezależna od Home Assistant,
- możliwa do odtworzenia po aktualizacji firmware.

Urządzenie nie może polegać na HA jako miejscu przechowywania swojej konfiguracji.

---

# 3. Podział danych

Dane dzielimy na trzy główne klasy:

```text
CONFIG
STATE
RUNTIME
```

## CONFIG

Dane trwałe ustawiane przez użytkownika lub system.

Przykłady:

- MQTT broker,
- username,
- password,
- buzzer_enabled,
- kalibracje,
- harmonogramy,
- limity,
- profile,
- ustawienia domenowe.

## STATE

Aktualny stan urządzenia, który zwykle można odtworzyć po restarcie.

Przykłady:

- aktualna temperatura,
- stan MQTT,
- Wi-Fi RSSI,
- aktualny tryb pracy,
- bieżący alarm zwykły.

STATE domyślnie nie jest zapisywany trwale.

## RUNTIME

Dane chwilowe potrzebne tylko podczas bieżącego działania.

Przykłady:

- timery,
- delay_on/off,
- retry counters,
- timestamp reconnect,
- bieżący ACK zwykłego alarmu,
- ring buffer diagnostyki.

RUNTIME nie jest zapisywany trwale.

---

# 4. Co zapisujemy

Trwale zapisujemy tylko dane, które rzeczywiście muszą przetrwać:

- restart,
- zanik zasilania,
- aktualizację firmware.

Typowe przykłady:

```text
network config
MQTT config
HA Discovery config
buzzer_enabled
domain settings
calibration data
schedules
profiles
hardware/user preferences
latched alarm persistence
```

---

# 5. Czego nie zapisujemy

Domyślnie nie zapisujemy:

- uptime,
- RSSI,
- aktualnego IP,
- chwilowego stanu MQTT,
- zwykłego ACK alarmu,
- runtime timers,
- aktywnego NORMAL/SERVICE,
- zwykłych alarmów auto-clear,
- historii runtime,
- transient error counters.

Jeśli coś można bezpiecznie odtworzyć po restarcie, nie powinno być zapisane bez potrzeby.

---

# 6. Tryb po restarcie

Zgodnie z [ARCHITECTURE.md](ARCHITECTURE.md) i
[NAMING_VERSIONING_STANDARD.md](NAMING_VERSIONING_STANDARD.md):

```text
mode = NORMAL
```

po każdym restarcie.

Tryb SERVICE nie jest zapisywany trwale.

---

# 7. Trwałość alarmów

Zgodnie z `ALARM_STANDARD`:

- zwykłe alarmy nie są utrwalane,
- zwykły ACK nie jest utrwalany,
- alarmy `latched` są utrwalane minimalnie,
- każdy `CRITICAL` jest traktowany jako latched.

Trwały zapis latcha powinien zawierać tylko dane niezbędne do jego odtworzenia.

---

# 8. Jeden wspólny system konfiguracji

Każdy projekt powinien używać wspólnego mechanizmu Core do:

- load,
- validate,
- migrate,
- save,
- reset,
- export,
- import.

Projekt domenowy nie powinien tworzyć własnego niezależnego systemu storage, jeśli nie ma wyjątkowego powodu.

---

# 9. Warstwy konfiguracji

Rekomendowany podział:

```text
CoreConfig
DomainConfig
PersistentState
```

## CoreConfig

Przykłady:

```text
network
mqtt
ha_discovery
buzzer
time
system preferences
```

## DomainConfig

Przykłady:

```text
Luma profiles
Doser calibration
Hydro thresholds
Clima limits
Gas calibration
Fauna feeding schedule
```

## PersistentState

Minimalny trwały stan, np.:

```text
latched alarms
calibration metadata
rare safety flags
```

---

# 10. config_schema_version

Każdy zapis konfiguracji posiada wersję schematu:

```text
config_schema_version
```

Wartość jest liczbą całkowitą.

Przykład:

```text
1
2
3
```

Nie używamy wersji typu `1.2.3` do wersjonowania struktury storage.

---

# 11. Firmware version ≠ config schema version

Rozdzielamy:

```text
firmware_version
core_version
config_schema_version
```

Zmiana firmware nie musi oznaczać zmiany schematu konfiguracji.

---

# 12. Migracje

Jeśli nowy firmware oczekuje nowszego schematu, Config/Application orchestruje migrację przez
project-specific typed adapter.

Przykład:

```text
v1 -> v2 -> v3
```

Preferujemy migracje krokowe zamiast skoku:

```text
v1 -> v3
```

Dzięki temu każda migracja jest prosta i testowalna.

---

# 13. Migracja automatyczna

Jeśli migracja jest bezpieczna i jednoznaczna, odbywa się automatycznie przy starcie.

Po udanej migracji:

```text
validate CURRENT snapshot
-> apply whole snapshot
-> save CURRENT canonical schema
-> continue boot
```

Stary poprawny rekord pozostaje nienaruszony do sukcesu migration, validation i apply. Błąd
zapisu CURRENT po udanym apply jest jawny jako PersistFailed/DEGRADED i nie zamienia starego
rekordu w uszkodzony.

---

# 14. Błąd migracji

Jeśli migracja nie może zostać wykonana bezpiecznie:

- urządzenie nie powinno losowo nadpisywać configu,
- Core loguje błąd,
- status może przejść w ERROR,
- jeśli konfiguracja jest krytyczna dla bezpieczeństwa, może powstać CRITICAL i safe state,
- użytkownik musi mieć możliwość naprawy przez WWW / restore / factory reset.

---

# 15. Brak migracji wstecz

Firmware nie musi wspierać automatycznej migracji konfiguracji do starszej wersji.

Downgrade firmware może wymagać:

- restore kompatybilnego backupu,
- factory reset.

Szczegóły downgrade należą do `OTA_STANDARD`.

---

# 16. Walidacja po odczycie

Po odczycie konfiguracji zawsze wykonujemy walidację.

Sprawdzamy:

- typy,
- zakresy,
- enumy,
- długości,
- zależności między polami,
- wersję schematu.

Nie ufamy samemu faktowi, że dane istnieją w flash.

---

# 17. Walidacja przed zapisem

Każda zmiana z WWW, MQTT lub innego źródła przechodzi walidację przed trwałym zapisem.

Sekwencja:

```text
input
-> validate
-> normalize
-> save
-> apply
```

---

# 18. Normalize

Jeśli wartość można jednoznacznie ustandaryzować, robimy to przed zapisem.

Przykłady:

```text
trim whitespace
uppercase enum
normalize IP/string format
clamp tylko tam, gdzie to jawnie bezpieczne
```

Nie poprawiamy ukrycie błędnych wartości, jeśli mogłoby to zaskoczyć użytkownika.

---

# 19. Atomic save

Konfiguracja logicznie należąca do jednego zestawu powinna być zapisywana atomowo.

Nie może dojść do stanu:

```text
50% nowego configu
50% starego configu
```

Preferowany model:

```text
write temp
validate/check
commit
```

Dokładna implementacja zależy od storage backendu.

---

# 20. Ochrona przed przerwaniem zasilania

System zapisu ma być odporny na utratę zasilania podczas save.

Po restarcie urządzenie powinno mieć:

- poprzednią poprawną konfigurację,
- albo nową poprawną konfigurację,

ale nie częściowo uszkodzony zestaw.

---

# 21. CRC / integrity check

Dla krytycznych struktur zalecane jest sprawdzanie integralności, np.:

```text
CRC
hash
storage backend integrity
```

Nie jest wymagane ciężkie kryptograficzne hashowanie, jeśli storage backend zapewnia wystarczającą ochronę przed uszkodzeniem.

---

# 22. Default config

Każdy projekt ma jawne wartości domyślne.

Domyślna konfiguracja powinna być tworzona przez kod, nie przez ukryte „magiczne” wartości rozsiane po projekcie.

Przykład:

```text
makeDefaultConfig()
```

---

# 23. Jedno źródło wartości domyślnych

Defaulty powinny być zdefiniowane w jednym miejscu.

Nie duplikujemy tych samych wartości w:

- backendzie,
- frontendzie,
- migracjach,
- kilku plikach projektu.

---

# 24. Factory defaults vs runtime fallback

Rozróżniamy:

```text
factory defaults
runtime fallback
```

Factory defaults są trwałymi wartościami startowymi po factory reset.

Runtime fallback może być chwilową wartością używaną, gdy sensor lub źródło danych jest niedostępne.

Nie mieszamy tych pojęć.

---

# 25. Factory reset

Factory reset:

- usuwa konfigurację użytkownika,
- usuwa trwałe ustawienia domenowe,
- usuwa MQTT credentials,
- usuwa trwałe latch state,
- przywraca factory defaults,
- przywraca `buzzer_enabled = ON`,
- ustawia tryb na NORMAL przy kolejnym uruchomieniu.

Szczegóły onboardingu po resecie definiuje `FACTORY_RESET_ONBOARDING_STANDARD`.

---

# 26. Co może pozostać po factory reset

Dane sprzętowe, których użytkownik nie konfiguruje, mogą pozostać.

Przykłady:

```text
hardware revision
factory calibration, jeśli wpisana produkcyjnie
device MAC
immutable hardware IDs
```

Nie usuwamy danych, których utrata mogłaby unieruchomić hardware.

---

# 27. Factory reset wymaga potwierdzenia

Factory reset musi wymagać wyraźnego potwierdzenia.

Nie może być wykonywany przez pojedynczy przypadkowy klik.

---

# 28. Backup konfiguracji

Jeśli projekt wspiera eksport konfiguracji, backup powinien zawierać:

```text
format_version
device_type
config_schema_version
core config
domain config
```

Opcjonalnie:

```text
hardware compatibility metadata
firmware version
core version
```

---

# 29. Backup nie powinien zawierać runtime

Backup nie zawiera:

- uptime,
- RSSI,
- aktualnych alarmów zwykłych,
- MQTT session state,
- historii runtime,
- tymczasowych liczników.

---

# 30. Sekrety w backupie

Zwykły backup lub eksport dostępny przez WWW NIE MOŻE zawierać sekretów w plaintext.

Może zawierać wyłącznie informację:

```text
configured = true
```

Po restore użytkownik może być zobowiązany do ponownego wpisania credentials.

Ewentualny pełny backup zawierający sekrety jest **FUTURE**, pozostaje poza zwykłym Web API
i poza zakresem v1. Wymaga osobnej, świadomie zatwierdzonej polityki bezpieczeństwa. Ten
standard nie definiuje obecnie jego formatu, szyfrowania ani mechanizmu kluczy.

---

# 31. Import konfiguracji

Import przebiega:

```text
parse
-> validate format
-> validate device_type
-> validate schema
-> migrate if needed
-> validate values
-> preview/result
-> save atomically
-> apply/restart if required
```

---

# 32. Import nie może nadpisywać na ślepo

Nie przyjmujemy backupu tylko dlatego, że ma poprawny JSON.

Należy sprawdzić kompatybilność.

---

# 33. device_type w backupie

Backup zawiera techniczny `device_type`, np.:

```text
luma
doser
hydro
clima
gas
fauna
```

Domyślnie nie można wgrać konfiguracji jednego typu urządzenia do innego.

---

# 34. Kompatybilność hardware

Jeśli konkretna konfiguracja zależy od hardware revision, backup powinien zawierać odpowiednie metadane.

Import nie może aktywować ustawień niezgodnych ze sprzętem.

---

# 35. Sekrety w API

Hasła i tokeny nigdy nie są zwracane w pełnej postaci przez zwykłe API WWW.

Storage może je przechowywać, ale UI nie powinno ich bez potrzeby ujawniać.

---

# 36. Logowanie

Nie logujemy:

- Wi-Fi password,
- MQTT password,
- tokenów,
- kluczy.

Log może zawierać np.:

```text
MQTT credentials updated
Wi-Fi config changed
```

bez wartości sekretu.

---

# 37. Minimalizacja zapisów flash

Nie zapisujemy flash przy każdej drobnej zmianie runtime.

Zapis następuje tylko wtedy, gdy trwała wartość rzeczywiście się zmieniła.

---

# 38. Brak zapisu niezmienionej wartości

Jeśli nowa wartość jest identyczna jak poprzednia:

```text
no write
```

Pozwala to ograniczyć zużycie flash.

---

# 39. Debounce zapisu

Dla ustawień, które mogą zmieniać się często, Core może stosować krótki debounce zapisu.

Przykład:

```text
user moves slider several times as proposals
-> validate final proposal after short idle
-> persist desired
-> apply final snapshot
```

Nie stosujemy tego do krytycznych zmian, które muszą być utrwalone natychmiast.
V1 nie traktuje live preview jako canonical config apply; ewentualny volatile preview wymaga
osobnego jawnego workflow.

---

# 40. Krytyczne ustawienia

Ustawienia bezpieczeństwa lub kalibracji, których utrata po zaniku zasilania byłaby problemem, powinny być zapisane od razu po świadomej zmianie.

---

# 41. Statystyki i liczniki

Długoterminowe liczniki nie należą automatycznie do CONFIG.

Jeśli projekt potrzebuje liczników trwałych:

```text
PersistentMetrics
```

powinny być osobną warstwą z własną polityką zapisu.

Nie zapisujemy ich przy każdym impulsie, jeśli grozi to zużyciem flash.

---

# 42. Storage backend

Core powinien ukrywać szczegóły backendu.

Projekt domenowy nie powinien wiedzieć, czy dane trafiają do:

- Preferences/NVS,
- LittleFS,
- innego backendu.

Projekt korzysta z warstwy konfiguracji Core.

---

# 43. Dobór backendu

Rekomendacja:

- małe klucze / proste ustawienia -> NVS/Preferences,
- większe struktury / profile / harmonogramy -> plikowy storage,
- nie mieszamy bez potrzeby wielu backendów dla tej samej klasy danych.

Dokładny wybór może zależeć od platformy i rozmiaru danych.

---

# 44. Jedna odpowiedzialność

Core powinien oddzielać:

```text
Config model
Serialization
Storage backend
Validation
Migration
```

Nie łączymy całej logiki w jednej klasie.

---

# 45. Rejestr konfiguracji

Docelowo Core powinien umożliwiać projektom rejestrację domenowych bloków konfiguracji.

Przykład logiczny:

```text
registerConfigSection(...)
```

Dokładne API może się różnić.

---

# 46. Namespace

Każda grupa configu powinna mieć stabilny namespace.

Przykładowo:

```text
system
network
mqtt
alarms
time
domain
```

Domena może używać własnego:

```text
luma
doser
hydro
```

---

# 47. Nazewnictwo kluczy

Klucze techniczne:

```text
snake_case
```

Powinny być:
- krótkie,
- jednoznaczne,
- stabilne.

Nie zmieniamy ich bez potrzeby, ponieważ wpływa to na migracje.

---

# 48. Brak bezpośrednich zapisów z domeny

Projekt domenowy nie powinien wykonywać:

```text
Preferences.put...
LittleFS.write...
```

bezpośrednio w logice biznesowej.

Zapis powinien przechodzić przez warstwę storage/config Core.

---

# 49. Aktualizacja konfiguracji w runtime

Po pełnej walidacji Config owner zapisuje desired config, a następnie stosuje cały snapshot.

Model:

```text
validate
-> persist desired
-> apply whole snapshot
-> publish immutable active
```

Persist failure nie zmienia active. Zmiana restart-required kończy się po persist jawnym
desired/active divergence i requestem `CONFIG_APPLY`, bez live apply.

---

# 50. Rollback konfiguracji przy apply failure

Jeśli persist się udał, ale apply kończy się błędem, persisted desired pozostaje zapisany,
poprzedni immutable ActiveConfig pozostaje logicznym active, a system raportuje jawne
`ApplyFailed + RecoveryRequired` i zabezpiecza affected subsystem. V1 nie wykonuje
automatycznego storage rollbacku ani nie zakłada hardware undo. Recovery może zapisać
poprawiony desired, wykonać jawny restore/factory reset albo kontrolowany restart.

---

# 51. Last known good config

CFG-101 v1 nie utrzymuje osobnego persistent `last_known_good` ani active marker.
ActiveConfig w RAM dowodzi ostatniego successful apply tylko w bieżącym runtime. Poprzedni
dual-slot record chroni zapis przed utratą zasilania, ale nie jest semantic last-known-good.
Dodatkowy mechanizm wymaga osobnej decyzji i wykazanej potrzeby.

---

# 52. Błąd odczytu storage

Jeśli storage nie może zostać odczytany:

- Core loguje błąd,
- nie zakłada, że storage jest pusty,
- nie nadpisuje automatycznie danych defaults,
- dla wymaganej sekcji wchodzi w ERROR recovery shell i pozostawia naprawę jawnemu workflow.

Nie wykonujemy cichego resetu configu bez śladu.

---

# 53. Defaults po błędzie storage

Jeśli użycie defaults mogłoby spowodować niebezpieczne działanie urządzenia:

- nie uruchamiamy domeny normalnie,
- generujemy ERROR/CRITICAL zgodnie z `ALARM_STANDARD`,
- wchodzimy w odpowiedni safe state.

Przy naprawdę pustym storage bezpieczne defaults są walidowane, applied i następnie zapisywane.
Przy obu corrupt slotach bezpieczne defaults mogą dać DEGRADED/RecoveryRequired, lecz nie są
automatycznie zapisywane nad uszkodzonymi rekordami.

---

# 54. Storage health

Core powinien udostępniać stan storage do diagnostyki.

Przykład:

```text
OK
RECOVERED
ERROR
```

Nie musi to być osobna encja HA, jeśli nie ma realnej potrzeby.

---

# 55. Atomicity per section

Nie zawsze trzeba zapisywać cały config urządzenia jako jeden wielki blob.

Preferowane są logiczne sekcje.

Przykład:

```text
network
mqtt
domain
```

Każda sekcja może być zapisywana atomowo niezależnie.

---

# 56. Brak gigantycznego config JSON w RAM

Na ESP unikamy modelu:

```text
serialize entire device config to huge JSON in RAM
```

jeśli można użyć:
- małych sekcji,
- streamingu,
- małych buforów.

---

# 57. Profile i większe dane

Większe struktury, np.:

- profile Luma,
- harmonogramy,
- tabele kalibracji,

mogą być przechowywane osobno od małego configu Core.

Nadal podlegają:
- wersjonowaniu,
- walidacji,
- migracjom.

---

# 58. Edycja większych struktur

Dla dużych sekcji preferujemy:

```text
load
-> edit in RAM
-> validate whole object
-> atomic replace
```

Nie zapisujemy pojedynczych bajtów na flash przy każdej zmianie pola.

---

# 59. Czas życia danych

Każda trwała wartość powinna mieć jasną odpowiedź na pytanie:

```text
dlaczego musi przetrwać restart?
```

Jeśli odpowiedź nie jest oczywista, najpewniej nie powinna być trwała.

---

# 60. Konfiguracja a HA

HA nie jest źródłem trwałej konfiguracji urządzenia.

Jeśli HA zmienia wartość trwałą, urządzenie:

```text
validate
-> save locally
-> apply
-> publish actual state
```

Po utracie HA wartość pozostaje w urządzeniu.

---

# 61. Konfiguracja a MQTT

MQTT command może zmieniać trwałą wartość tylko wtedy, gdy dana opcja jest jawnie wystawiona jako config/control.

Nie wystawiamy pełnej konfiguracji urządzenia przez MQTT.

Pełna konfiguracja techniczna pozostaje w WWW.

---

# 62. Konfiguracja a WWW

WWW jest podstawowym interfejsem pełnej konfiguracji technicznej.

Zmiany z WWW przechodzą dokładnie przez ten sam backend config/storage co zmiany z innych źródeł.

---

# 63. Apply source-independent

Niezależnie od źródła:

```text
WWW
HA
MQTT
physical UI
```

konfiguracja musi przejść przez wspólny mechanizm:

```text
validate
save
apply
publish state
```

---

# 64. Brak duplikacji configu

Nie utrzymujemy osobnych kopii tego samego ustawienia dla:

- WWW,
- MQTT,
- HA,
- domeny.

Istnieje jeden persisted desired config i jeden immutable active snapshot pełniące jawnie różne
role lifecycle. Nie istnieją konkurencyjne kopie per transport.

---

# 65. Boot sequence

Rekomendowana sekwencja:

```text
storage init
-> load raw config + stored schema version
-> integrity check and version classification
-> decode
-> migrate N -> N+1 when old-supported
-> validate CURRENT snapshot
-> apply whole snapshot
-> persist CURRENT form after successful migration/default apply
-> restore minimal persistent state
-> initialize services
-> initialize domain
-> publish ready/online
```

---

# 66. Nie publikujemy ready przed configiem

Urządzenie nie powinno zgłaszać się jako w pełni gotowe przed:

- poprawnym odczytem konfiguracji,
- migracją,
- walidacją,
- apply wymaganych sekcji i rozstrzygnięciem ich persistence result.

To samo dotyczy MQTT online zgodnie z `MQTT_STANDARD`.

---

# 67. Config dirty flag

Core może utrzymywać lekki stan:

```text
config_dirty
```

tylko runtime.

Służy do kontroli, czy istnieją zmiany oczekujące na zapis.

Nie musi być publikowany do HA.

---

# 68. Lifecycle i save result

Każdy etap lifecycle zwraca jednoznaczny wynik oraz potrzebne ortogonalne status flags; nie
tworzymy jednego mega-enum.

Przykłady:

```text
OK
NO_CHANGE
VALIDATION_ERROR
STORAGE_ERROR
MIGRATION_ERROR
APPLY_ERROR
RESTART_REQUIRED
RECOVERY_REQUIRED
```

Projekt domenowy nie powinien ignorować błędu zapisu.

---

# 69. API błędów

Backend WWW powinien mapować wyniki zapisu na canonical error envelope z
[WEB_STANDARD.md](WEB_STANDARD.md).

Przykład:

```json
{
  "ok": false,
  "error": {
    "code": "validation_error",
    "message": "Validation failed",
    "field": "max_temperature"
  }
}
```

`code` jest obowiązkowy, stabilny i machine-readable. `message` oraz `field` są opcjonalne.
CONFIG_STORAGE_STANDARD nie definiuje osobnego formatu błędów Web API.

---

# 70. Obsługa partial corruption

Jeśli jedna sekcja konfiguracji jest uszkodzona, Core powinien w miarę możliwości izolować problem do tej sekcji.

Nie należy automatycznie kasować całego configu urządzenia z powodu jednego niekrytycznego bloku.

---

# 71. Krytyczne vs niekrytyczne sekcje

Sekcje można traktować różnie.

Przykład:
- MQTT config uszkodzony -> urządzenie może działać autonomicznie bez MQTT,
- krytyczna kalibracja sensora bezpieczeństwa uszkodzona -> może wymagać safe state.

---

# 72. Recovery priority

CFG-101 używa deterministycznej klasyfikacji:

```text
current valid -> validate and apply
old supported -> migrate in RAM -> validate -> apply -> persist CURRENT
empty -> safe defaults -> validate -> apply -> persist
corrupt -> safe defaults only as DEGRADED, without automatic overwrite
future / migration failure / backend read failure -> preserve -> ERROR recovery
```

V1 nie ma osobnego persistent last-known-good.

---

# 73. Brak cichego factory reset

Core nie powinien sam wykonywać pełnego factory reset przy pierwszym błędzie storage.

To jest ostateczność.

---

# 74. Persisted alarm writes

Stan latched zapisujemy tylko przy zmianie:

```text
set latch
clear latch
```

Nie zapisujemy go w każdej iteracji loop.

---

# 75. Write rate limiting

Core może mieć wspólny mechanizm ochrony przed zbyt częstym zapisem.

Jeśli moduł próbuje zapisywać tę samą sekcję zbyt często, można:
- scalać zmiany,
- debounce,
- logować ostrzeżenie.

---

# 76. Thread/task safety

Jeśli urządzenie korzysta z wielu tasków, storage/config musi mieć jasno określony model synchronizacji.

Nie dopuszczamy równoczesnych nieskoordynowanych zapisów tej samej sekcji.

---

# 77. Reentrancy

Callback `apply` nie powinien ponownie wywoływać save tej samej sekcji w sposób prowadzący do pętli.

Core powinien chronić przed takimi przypadkami.

---

# 78. Testowalność

Warstwa config/storage musi być testowalna niezależnie od fizycznego ESP.

Należy testować co najmniej:
- defaults,
- validate,
- migrate,
- atomic save,
- corrupted data,
- interrupted save,
- import,
- factory reset,
- no-change write,
- desired/active divergence,
- persist failure bez apply,
- apply failure i recovery,
- power-loss recovery.

---

# 79. Test migracji

Każda zmiana `config_schema_version` musi mieć test:

```text
old config
-> migrate
-> validate
-> expected new config
```

---

# 80. Dokumentacja migracji

Każda migracja powinna mieć krótki opis:

```text
v1 -> v2
added mqtt_protocol_version
renamed x to y
default z = ...
```

Pozwala to śledzić ewolucję projektu.

---

# 81. Config schema changelog

Projekt może utrzymywać prosty changelog schematu konfiguracji.

Nie musi być osobnym dokumentem dla każdego drobiazgu, ale zmiany wersji powinny być łatwe do znalezienia.

---

# 82. Core API — odpowiedzialność

`aquaOneCore` powinien docelowo zapewnić mechanizmy odpowiadające za:

```text
ConfigManager
StorageBackend
ConfigValidator
MigrationRegistry
PersistentStateStore
BackupRestoreService
FactoryResetService
```

Nazwy klas mogą się różnić, ale podział odpowiedzialności powinien pozostać.

---

# 83. DomainConfig

Każdy projekt domenowy definiuje własny model konfiguracji.

Core nie powinien znać semantyki np.:

- dawki nawozu,
- profilu światła,
- poziomu CO2.

Core odpowiada za mechanizm, domena za znaczenie.

---

# 84. Rejestr migracji

Migracje powinny być rejestrowane w sposób deterministyczny.

Przykład logiczny:

```text
registerMigration(1, 2, ...)
registerMigration(2, 3, ...)
```

Core wykonuje je kolejno.

---

# 85. Nie zmieniamy istniejących danych bez potrzeby

Migracja powinna zmieniać tylko to, co rzeczywiście wymaga zmiany.

Nie przepisujemy całej konfiguracji przy każdej aktualizacji firmware.

---

# 86. Stabilność identyfikatorów

Nazwy pól i sekcji stanowią część trwałego kontraktu.

Zmiana nazwy pola wymaga migracji.

Dlatego:
- nazywamy pola technicznie,
- nie używamy nazw tymczasowych,
- unikamy częstych rename.

---

# 87. Lekkość dla ESP

Standard ma być lekki:

- małe sekcje config,
- minimalne zapisy flash,
- brak ciężkich baz danych,
- brak nadmiarowych kopii,
- brak ogromnych JSON-ów w RAM,
- proste migracje,
- tylko potrzebne metadane.

---

# 88. Integracja z WEB_STANDARD

WWW:
- edytuje config,
- backend waliduje,
- zapis jest atomiczny,
- apply jest jawny,
- sekrety nie są zwracane,
- błędy są czytelne.

---

# 89. Integracja z MQTT_STANDARD

MQTT:
- nie przechowuje konfiguracji urządzenia,
- może zmieniać tylko wybrane exposed settings,
- actual state pozostaje źródłem prawdy,
- reconnect nie nadpisuje configu.

---

# 90. Integracja z ALARM_STANDARD

Storage error może stać się:
- ERROR,
- CRITICAL,

zależnie od wpływu na bezpieczeństwo.

Latched CRITICAL persistence korzysta z tego standardu.

---

# 91. Integracja z OTA_STANDARD

Przed OTA konfiguracja musi być w spójnym stanie.

Po aktualizacji firmware:

```text
load
-> migrate
-> validate
-> start
```

Jeśli migracja nie powiedzie się, urządzenie nie może wejść w niekontrolowany normalny tryb pracy.

---

# 92. Reguła końcowa

Trwała konfiguracja ma być traktowana jak część kontraktu urządzenia, a nie przypadkowy zestaw wpisów w NVS.

**Core zarządza bieżącym mechanizmem rekordu, CRC, wyborem slotu i atomowym zapisem. Domena
definiuje dane, walidację, wartości domyślne oraz obecnie także migrację. Funkcje TARGET nie
mogą być raportowane jako zaimplementowane.**
