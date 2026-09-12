# OTA_STANDARD.md

**Status:** DRAFT
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard aktualizacji OTA dla całego ekosystemu **aquaOne**.

Dotyczy wszystkich urządzeń korzystających z `aquaOneCore`, m.in.:

- `aquaOneLuma`
- `aquaOneDoser`
- `aquaOneHydro`
- `aquaOneClima`
- `aquaOneGas`
- `aquaOneFauna`

Standard określa:

- sposób prowadzenia aktualizacji firmware,
- bezpieczeństwo OTA,
- rollback,
- walidację obrazu,
- zachowanie urządzenia podczas aktualizacji,
- relację OTA z konfiguracją i storage,
- statusy diagnostyczne,
- sposób prezentacji OTA w WWW,
- zachowanie po błędzie aktualizacji,
- odpowiedzialność `aquaOneCore` i projektów domenowych.

## Stan implementacji

### Implementation: CURRENT — Doser W1.5

Doser udostępnia przez wspólny AquaCore Web: Basic Auth przed upload START, walidację nazwy
`.bin`, deklarowanego rozmiaru i dostępnego miejsca, streaming START/CHUNK/END/ABORT,
`Update.begin/write/end`, cleanup przez `Update.abort`, wejście schedulera w maintenance,
zatrzymanie pomp oraz opóźniony restart. Ten zakres został zwalidowany sprzętowo 2026-09-12.

### Implementation: REQUIRED NEXT

W2 MUSI zachować obecny kontrakt auth, lifecycle, cleanup, bezpieczeństwa domenowego i
jednego fizycznego serwera. Kryteria transportu definiuje [WEB_STANDARD.md](WEB_STANDARD.md),
a dowody [TEST_STANDARD.md](TEST_STANDARD.md).

### Implementation: TARGET / FUTURE

Podpis obrazu, kryptograficzna integralność, rollback, pending validation, mark-valid,
self-test po boot i rozbudowana polityka kompatybilności nie są obecnie zaimplementowane.
Ich wymagania pozostają TARGET/FUTURE i korzystają z
[NAMING_VERSIONING_STANDARD.md](NAMING_VERSIONING_STANDARD.md) oraz
[SAFETY_STANDARD.md](SAFETY_STANDARD.md).

---

# 2. Zasada nadrzędna

OTA ma być:

- bezpieczne,
- przewidywalne,
- odporne na przerwanie zasilania,
- odporne na błędny firmware,
- niezależne od Home Assistant,
- możliwe do wykonania lokalnie,
- zgodne z rollbackiem platformy,
- lekkie dla ESP.

Aktualizacja firmware nigdy nie może być ważniejsza niż bezpieczeństwo urządzenia.

---

# 3. OTA nie jest wymagane do działania urządzenia

Brak możliwości wykonania OTA nie może blokować normalnego autonomicznego działania urządzenia.

OTA jest funkcją serwisową.

---

# 4. Główny interfejs OTA

Podstawowym interfejsem OTA jest lokalne WWW.

Home Assistant może w przyszłości wywoływać aktualizację, ale nie jest wymagany.

Podstawowa ścieżka:

```text
WWW
-> upload firmware
-> validate
-> prepare device
-> write inactive partition
-> verify
-> mark pending
-> reboot
-> self-test
-> mark valid
```

---

# 5. OTA tylko na wspieranej platformie

Core powinien używać natywnego mechanizmu OTA danej platformy.

Dla ESP32 preferowane jest wykorzystanie:

```text
ESP-IDF OTA / Arduino OTA partition API
```

Nie tworzymy własnego mechanizmu bootloadera bez potrzeby.

---

# 6. Partycje OTA

Urządzenie wspierające bezpieczne OTA powinno używać układu partycji pozwalającego na:

```text
running partition
inactive OTA partition
rollback
```

Aktualizacja nie powinna nadpisywać aktywnego obrazu bezpośrednio.

---

# 7. Rollback obowiązkowy tam, gdzie platforma wspiera

Jeśli platforma wspiera rollback firmware, mechanizm powinien być używany.

Nowy firmware po pierwszym starcie działa jako:

```text
pending verification
```

Dopiero po przejściu podstawowego self-testu zostaje oznaczony jako poprawny.

---

# 8. Kiedy firmware uznajemy za valid

Nowa wersja może zostać oznaczona jako valid dopiero po:

- poprawnym uruchomieniu systemu,
- odczycie konfiguracji,
- zakończeniu migracji configu,
- inicjalizacji wymaganych usług,
- inicjalizacji wymaganych modułów,
- pierwszej wiarygodnej ocenie alarmów,
- braku krytycznego błędu boot.

Nie wymagamy połączenia z HA ani MQTT, jeśli urządzenie działa autonomicznie.

---

# 9. Brak zależności od Internetu po aktualizacji

Firmware nie może pozostać pending tylko dlatego, że:

- nie ma Internetu,
- nie ma MQTT,
- broker jest niedostępny,
- HA jest offline.

Walidacja OTA opiera się na stanie lokalnym urządzenia.

---

# 10. Timeout walidacji po OTA

Nowy firmware nie może pozostawać bez końca w stanie pending.

Core powinien mieć rozsądny timeout walidacji.

Jeśli w tym czasie nie osiągnie wymaganej gotowości:

```text
rollback
```

jeśli platforma to wspiera.

---

# 11. Crash przed mark valid

Jeśli nowy firmware:

- panicuje,
- resetuje się przez watchdog,
- wpada w crash loop,
- nie kończy bootu,

zanim zostanie oznaczony jako valid, platforma powinna wrócić do poprzedniego firmware.

---

# 12. OTA a config migration

Po pierwszym starcie nowego firmware kolejność jest:

```text
load config
-> detect schema version
-> migrate
-> validate
-> initialize
-> self-test
-> mark firmware valid
```

Firmware nie może zostać oznaczony jako valid przed upewnieniem się, że konfiguracja jest używalna.

---

# 13. Problem migracji configu po OTA

Jeśli migracja configu się nie powiedzie:

- firmware nie powinien zostać oznaczony jako valid,
- urządzenie nie powinno wejść w normalną pracę,
- jeśli rollback jest dostępny, preferowany jest rollback,
- jeśli rollback nie jest możliwy, urządzenie przechodzi do bezpiecznego trybu serwisowego/safe state.

---

# 14. Migracja configu a rollback

Migracje powinny być projektowane tak, aby rollback firmware nie niszczył możliwości uruchomienia poprzedniej wersji.

Preferowane strategie:

- migracja dopiero po pełnej walidacji możliwości,
- backup starej sekcji przed zmianą,
- zachowanie last known good,
- migracje krokowe,
- brak destrukcyjnych zmian przed mark valid, jeśli można ich uniknąć.

---

# 15. Nieodwracalna migracja

Jeśli migracja jest nieodwracalna, musi być jawnie oznaczona i dobrze przetestowana.

Dla krytycznych danych preferowane jest:

```text
copy
-> migrate
-> validate
-> switch active
```

zamiast modyfikacji in-place.

---

# 16. Backup konfiguracji przed OTA

Przed OTA urządzenie powinno upewnić się, że konfiguracja jest zapisana i spójna.

Nie wymagamy automatycznego eksportu pliku backupu przy każdej aktualizacji.

Dla krytycznych sekcji można utrzymać wewnętrzne `last_known_good`.

---

# 17. OTA nie zapisuje przypadkowo configu

Samo uruchomienie procesu OTA nie może powodować dodatkowych niepotrzebnych zapisów konfiguracji.

---

# 18. Stan urządzenia przed OTA

Przed rozpoczęciem zapisu firmware Core powinien sprawdzić, czy aktualizacja jest bezpieczna.

Przykładowe warunki:

```text
no critical write in progress
storage healthy
enough free partition space
firmware file accepted
device not in unsafe transient operation
```

---

# 19. Domenowy prepareForOta

Projekt domenowy może dostarczyć callback:

```text
prepareForOta()
```

który bezpiecznie zatrzymuje proces.

Przykłady:

- zatrzymanie pomp,
- zatrzymanie dozowania,
- wyłączenie CO2,
- wyłączenie grzania,
- ustawienie LED w bezpieczny stan,
- zakończenie zapisu profilu.

---

# 20. OTA a CRITICAL

Jeśli aktywny jest `CRITICAL` lub `safety_lock`, OTA może być dozwolone tylko wtedy, gdy aktualizacja nie pogorszy bezpieczeństwa.

Domyślnie:

```text
OTA podczas safety_lock = dozwolone serwisowo
```

ale domenowe wyjścia pozostają w safe state przez cały proces.

OTA nie może zdejmować safety_lock.

---

# 21. OTA a SERVICE

Nie wymagamy przejścia w SERVICE przed OTA.

Core może wewnętrznie przejść w specjalny stan:

```text
UPDATING
```

niezależny od NORMAL/SERVICE.

Po restarcie zgodnie z kontraktem domenowym urządzenia tryb wraca do:

```text
NORMAL
```

chyba że safety_lock nadal obowiązuje.

---

# 22. OTA state

Wspólny stan OTA:

```text
IDLE
PREPARING
UPLOADING
VERIFYING
PENDING_REBOOT
PENDING_VALIDATION
VALID
FAILED
ROLLBACK
```

Nie wszystkie platformy muszą wewnętrznie implementować wszystkie stany, ale UI powinno mieć spójną semantykę.

---

# 23. ota_progress

Podczas uploadu można raportować:

```text
0..100 %
```

`ota_progress` jest stanem runtime.

Nie zapisujemy go trwale.

---

# 24. OTA w WWW

Sekcja OTA powinna pokazywać:

- current firmware version,
- core version,
- hardware revision, jeśli znana,
- status OTA,
- pole wyboru pliku,
- przycisk rozpoczęcia aktualizacji,
- progress,
- wynik,
- informację o restarcie.

---

# 25. Potwierdzenie OTA

Aktualizacja firmware powinna wymagać świadomego działania użytkownika.

Nie musi wymagać podwójnego potwierdzenia jak factory reset, ale nie powinna uruchamiać się przypadkowo po samym wyborze pliku.

---

# 26. Drag-and-drop opcjonalny

WWW może wspierać drag-and-drop, ale nie jest to wymaganie standardu.

Najważniejsza jest niezawodność i prostota.

---

# 27. Walidacja pliku przed zapisem

Przed rozpoczęciem zapisu Core powinien sprawdzić co najmniej:

- czy plik istnieje,
- czy ma poprawny typ/format,
- czy mieści się w partycji,
- czy nagłówek firmware jest poprawny,
- czy obraz jest przeznaczony dla zgodnej platformy.

---

# 28. device_type compatibility

Jeśli firmware zawiera metadane typu urządzenia, należy sprawdzić:

```text
device_type
```

Przykłady:

```text
luma
doser
hydro
clima
gas
fauna
```

Nie należy pozwalać na przypadkowe wgranie firmware innego typu urządzenia.

---

# 29. hardware compatibility

Jeśli firmware zależy od konkretnej rewizji sprzętu, należy sprawdzić:

```text
hardware_revision
```

Nie wolno aktywować firmware niezgodnego ze sprzętem.

---

# 30. Firmware metadata

Docelowo obraz lub towarzyszący manifest powinien zawierać co najmniej:

```text
firmware_version
device_type
core_version
```

Opcjonalnie:

```text
hardware_min
hardware_max
build_id
build_timestamp
config_schema_min
config_schema_target
```

---

# 31. build_id

Rekomendowany techniczny `build_id` powinien jednoznacznie identyfikować build.

Może zawierać np.:

```text
git commit short hash
```

Nie musi być encją HA.

---

# 32. SemVer firmware

Rekomendowany format wersji firmware:

```text
MAJOR.MINOR.PATCH
```

np.:

```text
1.4.2
```

Szczegóły wspólnego versioningu mogą zostać doprecyzowane w `NAMING_VERSIONING_STANDARD`.

---

# 33. Downgrade

Downgrade firmware nie powinien być domyślnie blokowany technicznie, ale musi być traktowany ostrożnie.

Przed downgrade należy sprawdzić kompatybilność:

- hardware,
- config schema,
- storage.

---

# 34. Ostrzeżenie przy downgrade

WWW powinno rozpoznać, jeśli użytkownik próbuje wgrać starszą wersję.

Powinno pokazać ostrzeżenie:

```text
Older firmware detected
```

i wymagać dodatkowego potwierdzenia.

---

# 35. Downgrade niezgodny z config schema

Jeśli starszy firmware nie potrafi obsłużyć obecnego config schema:

- aktualizacja powinna zostać zablokowana,
- albo użytkownik musi świadomie wykonać factory reset / restore zgodnego backupu.

Nie próbujemy losowo interpretować nowszej konfiguracji.

---

# 36. Integralność obrazu

Po zapisie obrazu firmware powinien zostać zweryfikowany.

Preferujemy mechanizmy platformy:

```text
image validation
partition verification
hash/checksum
```

---

# 37. Podpis firmware

Architektura powinna pozwalać w przyszłości na podpisane firmware.

W pierwszym etapie podpis kryptograficzny nie musi być obowiązkowy, jeśli urządzenia są aktualizowane lokalnie w zaufanej sieci.

Jeśli OTA będzie kiedyś wykonywane z Internetu, podpis staje się wymaganiem bezpieczeństwa.

---

# 38. Brak OTA z dowolnego URL jako domyślna funkcja

W v1 nie wymagamy:

```text
download firmware from arbitrary URL
```

Lokalny upload przez WWW jest prostszy i bezpieczniejszy.

---

# 39. OTA przez GitHub / manifest w przyszłości

Core może w przyszłości wspierać:

- sprawdzanie wersji,
- manifest release,
- pobieranie obrazu z zaufanego źródła.

Nie jest to częścią minimalnego standardu v1.

---

# 40. Brak automatycznej aktualizacji w tle

Urządzenie nie aktualizuje firmware samodzielnie bez zgody użytkownika.

Domyślnie:

```text
manual OTA only
```

---

# 41. Powiadomienie o dostępnej wersji

W przyszłości można dodać diagnostyczny:

```text
update_available
```

ale samo wykrycie nowej wersji nie uruchamia aktualizacji.

---

# 42. Zasilanie podczas OTA

Nie ma możliwości programowej gwarancji stabilnego zasilania, ale UI powinno ostrzegać, aby nie odłączać urządzenia podczas aktualizacji.

Dzięki dual-partition przerwanie zapisu nie powinno uszkodzić aktywnego firmware.

---

# 43. Zachowanie przy utracie połączenia WWW

Jeśli przeglądarka straci połączenie podczas uploadu:

- zapis powinien zostać anulowany bez aktywacji niepełnego obrazu,
- aktywny firmware pozostaje bez zmian,
- urządzenie wraca do normalnej pracy lub bezpiecznego stanu.

---

# 44. Niepełny obraz nie może być bootowalny

Nie ustawiamy partycji bootowej na nowy obraz przed pełnym zakończeniem i walidacją zapisu.

---

# 45. Restart po OTA

Po udanym OTA wymagany jest kontrolowany restart.

Sekwencja:

```text
finish upload
-> verify
-> prepare reboot
-> publish controlled offline if MQTT active
-> reboot
```

---

# 46. MQTT przed restartem

Jeśli MQTT jest online i jest czas na kontrolowane zakończenie:

- publikujemy availability offline,
- kończymy połączenie,
- potem restart.

Nie opóźniamy restartu bez końca, jeśli broker nie odpowiada.

---

# 47. OTA nie zależy od MQTT

Brak MQTT nie może blokować restartu po udanym OTA.

---

# 48. Logowanie OTA

Core powinien logować najważniejsze etapy:

```text
OTA started
image accepted
upload complete
verification passed
reboot requested
pending validation
firmware marked valid
OTA failed
rollback detected
```

Bez logowania sekretów.

---

# 49. ota_last_result

Core może utrzymywać diagnostyczny stan ostatniej aktualizacji:

```text
NONE
SUCCESS
FAILED
ROLLED_BACK
```

Jeśli potrzebne, może być utrwalony lekko, np. jako mały marker.

---

# 50. ota_last_error

Krótki stabilny kod błędu, np.:

```text
INVALID_IMAGE
WRONG_DEVICE_TYPE
WRONG_HARDWARE
NO_SPACE
WRITE_FAILED
VERIFY_FAILED
MIGRATION_FAILED
VALIDATION_TIMEOUT
```

---

# 51. OTA a alarmy

Błąd OTA sam w sobie zwykle nie jest alarmem, jeśli urządzenie nadal działa na poprzednim poprawnym firmware.

To diagnostyka.

Alarm jest uzasadniony, jeśli problem OTA wpływa na bezpieczne działanie urządzenia.

---

# 52. Rollback jako diagnostyka

Jeśli urządzenie uruchomiło poprzedni firmware po rollbacku:

- zapisujemy `ota_last_result = ROLLED_BACK`,
- logujemy powód, jeśli dostępny,
- pokazujemy to w WWW Diagnostics.

---

# 53. Rollback nie może wyczyścić configu

Rollback firmware nie wykonuje automatycznie factory reset.

Konfiguracja ma zostać zachowana, jeśli pozostaje kompatybilna.

---

# 54. Recovery po nieudanym OTA

Jeśli nowy firmware nie działa, priorytet odzyskiwania:

```text
rollback to previous valid firmware
-> safe boot / service mode
-> manual recovery
```

Nie przechodzimy od razu do factory reset.

---

# 55. Safe boot

Core może w przyszłości wspierać lekki safe boot, który:

- uruchamia WWW,
- uruchamia storage/config,
- uruchamia diagnostykę,
- nie aktywuje niebezpiecznych funkcji domenowych.

Może służyć do naprawy configu lub ponownego OTA.

---

# 56. Safe boot nie jest normalnym SERVICE

Safe boot to techniczny tryb odzyskiwania.

Nie należy go mylić z domenowym `SERVICE`.

---

# 57. Watchdog podczas OTA

Proces OTA nie powinien powodować watchdog reset.

Długie operacje muszą być wykonywane w sposób zgodny z watchdogiem platformy.

Nie wolno bezmyślnie wyłączać watchdogów na cały proces OTA.

---

# 58. Responsywność podczas OTA

Podczas uploadu urządzenie może ograniczyć inne funkcje WWW.

Domena jednak musi pozostawać w bezpiecznym stanie.

---

# 59. Blokada równoległych aktualizacji

Core nie może pozwolić na rozpoczęcie drugiego OTA, gdy jedno jest w toku.

Stan:

```text
BUSY
```

---

# 60. Blokada konfliktujących operacji

Podczas OTA należy blokować m.in.:

- factory reset,
- restore config,
- drugi OTA,
- restart ręczny,
- krytyczne zapisy storage.

---

# 61. Akcje domenowe podczas OTA

Domyślnie akcje domenowe są blokowane, jeśli mogłyby kolidować z bezpiecznym stanem aktualizacji.

Projekt może jawnie dopuścić tylko bezpieczne odczyty.

---

# 62. Read-only diagnostics podczas OTA

Diagnostyka może pozostać dostępna w trybie read-only, jeśli nie komplikuje implementacji.

---

# 63. OTA progress a RAM

Progress nie wymaga przechowywania dużych struktur.

Wystarczy:

```text
bytes_received
total_bytes
percent
```

---

# 64. Streaming upload

Firmware powinien być zapisywany strumieniowo.

Nie przechowujemy całego obrazu w RAM.

---

# 65. Limit rozmiaru

Core sprawdza maksymalny rozmiar obrazu względem dostępnej partycji.

Zbyt duży obraz jest odrzucany przed aktywacją.

---

# 66. Nazwa pliku nie jest źródłem prawdy

Nie ufamy nazwie pliku typu:

```text
aquaOneLuma_v1.2.3.bin
```

Metadane i walidacja obrazu są ważniejsze.

---

# 67. OTA a filesystem

Jeśli firmware i filesystem są aktualizowane osobno, standard powinien traktować je jako dwie różne operacje.

Nie aktualizujemy filesystemu bez potrzeby.

---

# 68. Bundle firmware + assets

Jeśli WWW assets są częścią firmware, preferujemy jedną spójną aktualizację firmware.

Zmniejsza to ryzyko niezgodności wersji UI/backend.

---

# 69. Aktualizacja Core jako część firmware

`aquaOneCore` nie jest aktualizowany osobno.

Jego wersja jest częścią builda projektu domenowego.

---

# 70. Wersja Core po OTA

Po restarcie WWW i diagnostyka pokazują nową:

```text
core_version
```

razem z:

```text
firmware_version
```

---

# 71. OTA a HA

HA może widzieć wersję firmware i ewentualnie informację diagnostyczną o aktualizacji.

Nie wymagamy tworzenia HA `update` entity w pierwszej wersji standardu.

---

# 72. HA update entity opcjonalna

Jeśli w przyszłości pojawi się stabilny manifest/release mechanism, Core może wystawiać:

```text
update entity
```

Nie może to zmienić zasady, że urządzenie działa bez HA.

---

# 73. OTA a MQTT

MQTT nie służy do przesyłania pliku firmware.

Może najwyżej:

- wywołać wcześniej przygotowaną aktualizację,
- raportować status,

jeśli taka funkcja zostanie świadomie dodana.

---

# 74. OTA zdalne

Zdalne OTA poza LAN wymaga dodatkowych zabezpieczeń.

Nie wystawiamy endpointu OTA bezpośrednio do Internetu jako zalecanego rozwiązania.

---

# 75. Autoryzacja OTA

Jeśli WWW otrzyma warstwę autoryzacji, endpoint OTA musi być chroniony co najmniej tak samo jak inne operacje administracyjne.

---

# 76. Factory reset a OTA rollback

Factory reset i OTA rollback to dwa niezależne mechanizmy.

Rollback:
- zmienia firmware.

Factory reset:
- resetuje konfigurację.

Nie łączymy ich automatycznie.

---

# 77. Testy OTA

Każdy projekt powinien testować co najmniej:

- poprawny update,
- przerwanie uploadu,
- zły plik,
- zły device_type,
- złą hardware revision,
- za duży obraz,
- błąd zapisu,
- restart po update,
- mark valid,
- rollback,
- config migration failure,
- brak MQTT/Internetu po update.

---

# 78. Test odcięcia zasilania

Jeśli możliwe, należy testować utratę zasilania:

- podczas uploadu,
- po zapisie przed reboot,
- podczas pierwszego bootu nowego firmware.

Urządzenie powinno wrócić do poprawnego obrazu lub recovery.

---

# 79. Test crash loop

Należy sprawdzić, że celowo wadliwy firmware, który resetuje się przed mark valid, wraca do poprzedniego obrazu.

---

# 80. Test config compatibility

Przed wydaniem nowej wersji testujemy:

```text
old config
-> OTA
-> migrate
-> run
```

oraz jeśli wspierany jest rollback:

```text
failed new firmware
-> rollback
-> previous firmware boot
```

---

# 81. Release candidate

Przed oznaczeniem wersji produkcyjnej warto testować build jako release candidate na realnym sprzęcie.

Nie jest to wymaganie runtime, ale dobra praktyka projektu.

---

# 82. Core API — odpowiedzialność

`aquaOneCore` powinien docelowo zapewnić mechanizmy odpowiadające za:

```text
OtaManager
FirmwareValidator
OtaState
OtaDiagnostics
OtaPlatformAdapter
PostUpdateValidator
RollbackCoordinator
```

Nazwy klas mogą się różnić, ale podział odpowiedzialności powinien pozostać.

---

# 83. OtaManager

Odpowiada za:

- rozpoczęcie aktualizacji,
- stan OTA,
- progress,
- streaming obrazu,
- walidację,
- przygotowanie reboot,
- diagnostykę błędów.

---

# 84. PostUpdateValidator

Odpowiada za decyzję, czy nowy firmware można oznaczyć jako valid.

Sprawdza m.in.:

- config,
- migracje,
- wymagane usługi,
- wymagane moduły,
- alarm manager,
- podstawowy health.

---

# 85. Domena — odpowiedzialność

Projekt domenowy odpowiada za:

- `prepareForOta`,
- bezpieczne zatrzymanie funkcji,
- hardware compatibility,
- domenowe warunki self-testu,
- wskazanie funkcji, które muszą pozostać zablokowane podczas OTA.

---

# 86. Minimalny stan bezpieczny podczas OTA

Jeśli projekt steruje fizycznym procesem, podczas OTA powinien przejść do bezpiecznego stanu.

Aktualizacja nie może pozostawiać aktuatora w przypadkowym stanie tylko dlatego, że CPU zajmuje się zapisem flash.

---

# 87. Brak automatycznego wznawiania procesu po OTA

Po restarcie urządzenie startuje zgodnie z normalną logiką boot.

Procesy nie powinny być automatycznie „wznowione od miejsca przerwania”, chyba że domena świadomie to zaprojektowała i jest to bezpieczne.

---

# 88. OTA marker

Core może używać małego trwałego markera:

```text
ota_pending
ota_last_result
```

tylko jeśli platforma nie zapewnia odpowiednich informacji natywnie.

Nie duplikujemy danych bez potrzeby.

---

# 89. Lekkość dla ESP

Implementacja ma być lekka:

- streaming zamiast całego obrazu w RAM,
- natywne OTA partitions,
- małe struktury stanu,
- krótkie kody błędów,
- brak chmurowego klienta aktualizacji w v1,
- brak automatycznego pobierania dużych manifestów,
- minimum zapisów storage.

---

# 90. Integracja z CONFIG_STORAGE_STANDARD

OTA respektuje:

- config schema version,
- migracje,
- atomic save,
- last known good,
- recovery.

Aktualizacja nie omija warstwy config/storage.

---

# 91. Integracja z DIAGNOSTICS_STANDARD

OTA dostarcza diagnostycznie:

```text
ota_state
ota_progress
ota_last_result
ota_last_error
firmware_version
core_version
```

Nie wszystkie pola muszą trafiać do HA.

---

# 92. Integracja z ALARM_STANDARD

OTA nie kasuje:

- aktywnych alarmów,
- latched alarms,
- safety_lock.

Po restarcie alarmy są ponownie oceniane zgodnie z `ALARM_STANDARD`.

---

# 93. Integracja z WEB_STANDARD

WWW jest głównym lokalnym interfejsem OTA.

UI:
- waliduje podstawowe dane,
- pokazuje progress,
- pokazuje wynik,
- nie ujawnia zbędnych szczegółów,
- nie pozwala na konfliktujące operacje.

---

# 94. Integracja z architekturą urządzenia

Po aktualizacji i restarcie:

```text
mode = NORMAL
```

zgodnie z [ARCHITECTURE.md](ARCHITECTURE.md) i kontraktem domenowym urządzenia.

Jeśli istnieje `safety_lock`, blokada bezpieczeństwa ma pierwszeństwo nad normalnym działaniem.

---

# 95. Reguła końcowa

OTA nie jest zwykłym „uploadem pliku”.

To kontrolowany proces:

```text
validate
-> secure state
-> write inactive image
-> verify
-> reboot
-> self-test
-> mark valid
-> rollback on failure
```

**Doser CURRENT zapewnia lokalny mechanizm W1.5 bez rollbacku. Wspólny Core OTA, signing,
post-boot validation i rollback pozostają TARGET. Domena zawsze definiuje wymagania
sprzętowe i bezpieczne zachowanie konkretnego urządzenia.**
