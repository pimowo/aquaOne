# WEB_STANDARD.md

**Status:** DRAFT
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard lokalnego interfejsu WWW dla całego ekosystemu **aquaOne**.

Dotyczy wszystkich urządzeń korzystających z `aquaOneCore`, m.in.:

- `aquaOneLuma`
- `aquaOneDoser`
- `aquaOneHydro`
- `aquaOneClima`
- `aquaOneGas`
- `aquaOneFauna`

Standard określa:

- rolę lokalnego WWW,
- wspólną strukturę UI,
- zasady konfiguracji,
- sposób prezentacji stanu i diagnostyki,
- reguły zapisu i walidacji,
- zachowanie bez sieci zewnętrznej,
- relację WWW z Home Assistant,
- odpowiedzialność `aquaOneCore` i projektów domenowych,
- wymagania dotyczące lekkości dla ESP.

## Stan implementacji

### Implementation: CURRENT

AquaCore 0.6.2 udostępnia jeden fizyczny `Esp32WebBackend` posiadający Arduino
`WebServer` oraz neutralny `WebService`. Bieżący transport obsługuje routing GET/POST,
Basic Auth z realm, nagłówki, `maxBodyLength` ustawiany per route dla zwykłego body oraz upload
lifecycle START/CHUNK/END/ABORT. `maxBodyLength = 0` zachowuje legacy behavior, a mechanizm
nie obejmuje multipart upload. Limity rejestru wynoszą obecnie 8 page providers, 8 API
providers i 24 routes. Core ma wbudowane `/`, `/assets/aqua.css`, `/api/system`
i `/api/diagnostics`.

Doser W1/W1.5 używa tego samego backendu dla `POST /api/restart`, `GET /update` i
`POST /update`. Domena Dosera posiada politykę auth, zatrzymanie pomp, maintenance,
firmware write, cleanup i opóźniony restart.

Hardware validation: **PASSED** 2026-09-12.
Evidence: not yet persisted in repository.

### Implementation: REQUIRED NEXT — W2

W2 obejmuje migrację pozostałych stron i API produktu Dosera bez uruchamiania drugiego
serwera HTTP i bez przenoszenia logiki domenowej do Core.

#### Kontrakt tras i ownership

- Każda trasa MUSI mieć jednego właściciela: Core albo konkretny provider domenowy.
- Rejestracja identycznej pary `method + path` MUSI zostać odrzucona przed startem obsługi.
- Trasy Core nie mogą być przesłaniane przez domenę.
- Przed implementacją W2 MUSI istnieć zatwierdzona tabela: method, path, owner, auth,
  request limit, response type, side effects i test acceptance.

#### Tabela tras W2

Poniższa tabela jest obowiązującym zakresem W2. Wiersz z `DECISION REQUIRED` nie może wejść
do implementacji, dopóki decyzja nie zostanie zatwierdzona i wpisana do tabeli.

| Method | Path | Owner | Auth | Request/body limit | Response type | Side effects | Concurrency/BUSY policy | Expected status codes | Acceptance test |
|---|---|---|---|---|---|---|---|---|---|
| GET | `/` | Core `WebService` | PUBLIC | Brak body | HTML | Brak | Odczyt współbieżny; bez BUSY | `200`, `500`, `503` | Root odpowiada przez jedyny backend i zawiera nawigację do providerów |
| GET | `/assets/aqua.css` | Core `WebService` | PUBLIC | Brak body | CSS | Brak; zasób statyczny | Odczyt współbieżny; bez BUSY | `200`, `404`, `500`, `503` | Arkusz stylów odpowiada przez jedyny backend i ma poprawny content type |
| GET | `/api/system` | Core `WebService` | PUBLIC | Brak body | JSON | Brak; snapshot read-only | Odczyt współbieżny; bez BUSY | `200`, `500`, `503` | Zwraca bieżący snapshot systemowy przez jedyny backend; brak sekretów |
| GET | `/api/diagnostics` | Core `WebService` | PUBLIC | Brak body | JSON | Brak; snapshot read-only | Odczyt współbieżny; bez BUSY | `200`, `500`, `503` | Zwraca bieżący snapshot diagnostyczny przez jedyny backend; brak sekretów |
| GET | `/api/status` | Doser domain provider | **DECISION REQUIRED** | Brak body | JSON | Brak; snapshot read-only | Odczyt współbieżny; bez BUSY | `200`, `401` jeśli AUTHENTICATED, `500`, `503` | Snapshot ma zatwierdzony schema; test public/auth zgodny z decyzją; brak sekretów |
| POST | `/api/restart` | Doser `WebManager` / domain policy | AUTHENTICATED | Bez body; żądanie z body odrzucone | JSON | Planowanie one-shot restart; pump stop przed restartem | Kolejne żądanie podczas pending restart: `409 busy` | `202`, `400`, `401`, `409`, `500`, `503` | Realm; odpowiedź przed restartem; one-shot; drugi request zwraca busy; pompy zatrzymane |
| GET | `/update` | Doser `WebManager` / domain policy | AUTHENTICATED | Brak body | HTML | Brak | Podczas aktywnego OTA: `409 busy` | `200`, `401`, `409`, `500`, `503` | Realm; formularz dostępny po auth; brak wpływu na aktywne OTA |
| POST | `/update` | Doser `WebManager` / domain policy | AUTHENTICATED | Multipart firmware; max upload size: **DECISION REQUIRED**; zwykły `maxBodyLength` nie jest limitem uploadu | HTML lub tekst dla wyniku strony; callback upload bez JSON envelope | Pump stop, maintenance, firmware write, cleanup, opóźniony restart po sukcesie | Jedno OTA naraz; drugi START: `409 busy`, bez resetu aktywnego uploadu | `200`, `400`, `401`, `409`, `413` po zatwierdzeniu limitu, `500`, `503` | Auth przy START; success/failure/ABORT/disconnect; drugi START nie resetuje uploadu; cleanup; restart i reconnect |

Tabela zawiera wszystkie trasy Core i Dosera znane w zakresie W2 na dzień przeglądu. Dodanie
innej migrowanej trasy wymaga najpierw dodania jej pełnego wiersza i zatwierdzenia tabeli.

#### Auth i ekspozycja

- Każda trasa MUSI być jawnie oznaczona jako `PUBLIC` albo `AUTHENTICATED`; brak deklaracji
  oznacza odrzucenie rejestracji.
- Endpointy mutujące stan, restart, OTA, sekrety, network i administracja MUSZĄ wymagać auth.
- Odpowiedź `401` MUSI zawierać właściwy challenge/realm. `403` oznacza użytkownika
  rozpoznanego, ale bez prawa do operacji.
- Hasła, tokeny i credentials nie mogą pojawić się w odpowiedzi, logu ani diagnostyce.

#### Limity, czas i współbieżność

- **CURRENT:** `WebRouteOptions::maxBodyLength` ogranicza zwykłe body per route; wartość zero
  zachowuje legacy behavior. Limit nie obejmuje multipart upload.
- **REQUIRED NEXT — W2:** każda trasa API przyjmująca zwykłe body MUSI mieć zatwierdzony,
  niezerowy `maxBodyLength`; przekroczenie zwraca `413 Payload Too Large` przed efektem.
- Firmware jest przetwarzany strumieniowo i nie może być buforowany w całości w RAM.
- Handler nie może używać nieograniczonego oczekiwania ani blokującego `delay()`.
- Przerwanie klienta lub ABORT MUSI zakończyć aktywny zapis i zwolnić zasoby.
- **REQUIRED NEXT — W2:** dozwolona jest najwyżej jedna operacja maintenance/OTA naraz.
  Drugi START zwraca `409 Conflict` z kodem `busy` i nie resetuje aktywnego uploadu. Obecny
  Doser resetuje upload state przy autoryzowanym START, więc nie jest to zachowanie CURRENT.
- Max upload size: **DECISION REQUIRED**.
- Upload inactivity timeout: **DECISION REQUIRED**.
- Całkowity upload timeout albo jawna decyzja o jego braku: **DECISION REQUIRED**.
- Sposób dostarczenia odpowiedzi HTTP `409` podczas drugiego multipart START:
  **DECISION REQUIRED**.

#### Odpowiedzi i błędy

Sukces API używa statusu `2xx`. Błąd MUSI użyć właściwego statusu `4xx`/`5xx` i stabilnej
koperty:

```json
{
  "ok": false,
  "error": {
    "code": "invalid_value",
    "message": "Invalid value",
    "field": "port"
  }
}
```

`message` i `field` są opcjonalne; `code` MUSI być stabilne, machine-readable i nie może
zawierać sekretów. Wszystkie endpointy API używają tej koperty. Odpowiedzi HTML lub tekstowe
spoza API, w tym strona `/update`, nie muszą używać JSON envelope.
Minimum: `400` invalid request, `401/403` auth, `404` route, `409` conflict/busy,
`413` body limit, `500` internal failure, `503` chwilowa niedostępność.

#### Restart i OTA

- Odpowiedź HTTP MUSI zostać zakończona przed restartem urządzenia.
- Żądanie restartu jest one-shot; ponowne wywołanie pętli nie może wykonać drugiego restartu.
- Auth uploadu MUSI zostać sprawdzone przy START przed zmianą aktywnego stanu OTA.
- CHUNK/END/ABORT bez autoryzowanego START nie może zmieniać aktywnej operacji.
- Każdy błąd po rozpoczęciu OTA MUSI wykonać cleanup i pozostawić domenę w zdefiniowanym
  bezpiecznym stanie. Szczegóły określa [OTA_STANDARD.md](OTA_STANDARD.md).

### Implementation: TARGET

Wspólny HTML shell, theme, komplet stron systemowych, rozbudowane API helpers oraz jednolite
UI wszystkich urządzeń są TARGET. Nie są warunkiem W2, jeśli nie wynikają z zatwierdzonej
tabeli tras Dosera.

## W2 entry criteria

- W1/W1.5 build i focused Web test: compile/link PASS, runtime NOT EXECUTED.
- Przed W2 focused Web test MUSI zostać wykonany runtime, a rozbieżność oczekiwania braku `/`
  po `WebService::begin()` MUSI zostać rozstrzygnięta. Dokument nie deklaruje runtime PASS.
- Hardware validation W1.5: PASSED dla auth, restart, OTA success, failure/abort, cleanup i
  reconnect. Evidence: not yet persisted in repository; raport MUSI zostać utrwalony zgodnie
  z [TEST_STANDARD.md](TEST_STANDARD.md).
- Zatwierdzona tabela wszystkich migrowanych tras i ownerów.
- Dla każdej trasy zdefiniowane auth, limity, efekty, statusy i test.
- Wszystkie pozycje `DECISION REQUIRED` w tabeli i polityce limitów są rozstrzygnięte.
- Potwierdzony jeden `Esp32WebBackend` i brak drugiego fizycznego `WebServer`.

## W2 acceptance criteria

- Wszystkie trasy z zatwierdzonej tabeli działają przez jeden backend bez kolizji.
- Public/private policy, realm, body limits, envelope i kody HTTP mają testy negatywne.
- Disconnect, timeout, ABORT, failure i BUSY nie pozostawiają aktywnego zapisu ani maintenance.
- Restart odpowiada przed rebootem i pozostaje one-shot.
- Regresja W1/W1.5 oraz produkcyjny build Dosera przechodzą.
- Test sprzętowy potwierdza strony/API W2, restart, OTA, reconnect i bezpieczny stan pomp.
- Evidence spełnia [TEST_STANDARD.md](TEST_STANDARD.md), w tym jawnie podaje liczbę
  wykonanych testów.

---

# 2. Zasada nadrzędna

Web jest opcjonalnym modułem technicznej kompozycji Core. Projekt urządzenia może go nie
używać, jeśli nie ma takiego wymagania produktowego. Brak Web nie może naruszać autonomii
logiki domenowej.

Jeśli projekt deklaruje lokalne WWW, MUSI stosować WEB_STANDARD. Dla takiego projektu WWW
jest preferowanym lokalnym interfejsem:

- konfiguracji technicznej,
- diagnostyki,
- serwisu,
- podglądu bieżącego stanu,
- czynności administracyjnych urządzenia.

WWW działa lokalnie i nie zależy od:

- Home Assistant,
- MQTT,
- Internetu,
- chmury.

Home Assistant jest dodatkowym interfejsem użytkownika, ale nie zastępuje lokalnego WWW.

---

# 3. Autonomia

Urządzenie musi pozostać w pełni funkcjonalne bez Home Assistant i bez MQTT.

Brak HA/MQTT:

- nie blokuje WWW,
- nie blokuje konfiguracji,
- nie blokuje lokalnej diagnostyki,
- nie blokuje działania domenowego.

---

# 4. Zakres odpowiedzialności WWW

WWW odpowiada za:

- konfigurację Wi-Fi,
- konfigurację MQTT,
- konfigurację HA Discovery,
- konfigurację urządzenia domenowego,
- ustawienia użytkownika,
- diagnostykę,
- alarmy,
- wersje firmware/Core,
- restart,
- factory reset,
- OTA,
- backup/restore konfiguracji, jeśli urządzenie to wspiera.

Nie każda sekcja musi być obecna w każdym urządzeniu. Projekty deklarują tylko funkcje, których rzeczywiście używają.

---

# 5. Wspólna struktura interfejsu

Docelowy układ WWW powinien być spójny w całym aquaOne.

Rekomendowane główne sekcje:

```text
Dashboard
Control
Settings
Alarms
Diagnostics
System
```

Nie wszystkie projekty muszą mieć `Control`, jeśli nie mają sensownego lokalnego sterowania.

---

# 6. Dashboard

Dashboard pokazuje skrót aktualnego stanu urządzenia.

Powinien zawierać tylko najważniejsze informacje:

- techniczną nazwę urządzenia,
- status,
- mode,
- safety_lock,
- alarm_severity,
- najważniejsze sensory/parametry domenowe,
- stan MQTT,
- stan Wi-Fi,
- uptime.

Dashboard nie może być przeładowany detalami diagnostycznymi.

---

# 7. Control

Sekcja `Control` służy do bezpośredniego sterowania funkcjami domenowymi.

Przykłady:

- ręczne uruchomienie pompy,
- wybór profilu,
- zmiana trybu,
- uruchomienie karmienia,
- ręczne sterowanie kanałem,
- akcja serwisowa.

Sterowanie z WWW musi przechodzić przez tę samą logikę domenową i te same zabezpieczenia co sterowanie z HA/MQTT.

WWW nie steruje hardware bezpośrednio z pominięciem logiki urządzenia.

---

# 8. Settings

Sekcja `Settings` obejmuje konfigurację trwałą.

Może zawierać podsekcje:

```text
Device
Network
MQTT
Domain
Time
Alarm/Buzzer
```

Projekt domenowy dodaje własne ustawienia bez zmiany wspólnej logiki Web Core.

---

# 9. Alarms

Sekcja `Alarms` jest zgodna z `ALARM_STANDARD.md`.

Ma co najmniej dwie sekcje:

```text
Active alarms
Recent events
```

Aktywne alarmy mogą pokazywać:

- severity,
- alarm_id,
- description,
- recommended_action,
- source value,
- ACK state,
- stan latch,
- safety_lock.

WWW umożliwia:

- ACK pojedynczego alarmu,
- ACK ALL,
- zmianę `buzzer_enabled`.

---

# 10. Diagnostics

Sekcja `Diagnostics` pokazuje techniczne informacje potrzebne do serwisu i debugowania.

Powinna być oddzielona od normalnego dashboardu.

Może zawierać:

- Wi-Fi RSSI,
- IP,
- MAC,
- uptime,
- reset reason,
- firmware version,
- core version,
- hardware revision,
- MQTT status,
- MQTT root,
- broker status,
- time/RTC/NTP state,
- storage/config state,
- ostatnie błędy,
- stan sensorów,
- heap / memory, jeśli ma to sens.

Nie wszystkie dane diagnostyczne muszą być publikowane do HA.

---

# 11. System

Sekcja `System` zawiera czynności administracyjne:

- restart,
- OTA,
- backup konfiguracji,
- restore konfiguracji,
- factory reset,
- informacje o wersjach.

Akcje destrukcyjne muszą wymagać potwierdzenia.

---

# 12. Spójny wygląd

Wszystkie urządzenia aquaOne powinny używać wspólnego stylu WWW.

Core powinien dostarczać wspólny shell/layout:

- nagłówek,
- nawigację,
- typografię,
- komponenty formularzy,
- karty,
- alerty,
- przyciski,
- style statusów,
- wspólne komponenty diagnostyczne.

Projekt domenowy dostarcza treść i logikę, nie własny całkowicie niezależny framework UI.

---

# 13. Lekkość dla ESP

WWW musi być lekkie.

Preferowane:

- statyczny HTML/CSS,
- mały JavaScript,
- brak dużych frameworków frontendowych,
- brak React/Vue/Angular,
- brak CDN jako wymagania,
- brak ciężkich grafik,
- brak dużych bibliotek runtime,
- krótkie odpowiedzi API,
- minimalna liczba endpointów i zapytań.

Interfejs musi działać bez Internetu.

---

# 14. Brak zależności od CDN

WWW nie może wymagać zewnętrznych zasobów do działania.

Nie wolno uzależniać interfejsu od:

- Google Fonts,
- Bootstrap CDN,
- zewnętrznego JS,
- zewnętrznych ikon,
- zewnętrznych API.

Wszystko wymagane do działania musi znajdować się lokalnie w firmware lub zasobach urządzenia.

---

# 15. Responsywność

WWW musi poprawnie działać na:

- telefonie,
- tablecie,
- komputerze.

Nie wymaga osobnych wersji strony.

Layout ma być responsywny i czytelny przy małej szerokości ekranu.

---

# 16. API-first wewnątrz urządzenia

WWW powinno używać prostego lokalnego API urządzenia zamiast mieszać renderowanie z logiką domenową.

Rekomendowany model:

```text
Web page
    ↓
HTTP API
    ↓
Application/domain API
    ↓
Device logic
```

Warstwa WWW nie powinna bezpośrednio manipulować hardware.

---

# 17. Oddzielenie frontend / backend

Core powinien rozdzielać:

- routing HTTP,
- API,
- HTML shell,
- stronę domenową,
- logikę urządzenia.

Projekt domenowy nie powinien kopiować całego WebService.

---

# 18. Wspólne API Core

`aquaOneCore` powinien zapewniać mechanizmy do:

- rejestracji stron,
- rejestracji endpointów API,
- wspólnego HTML shell,
- odpowiedzi JSON,
- walidacji danych wejściowych,
- zwracania błędów,
- restartu,
- informacji systemowych,
- diagnostyki,
- MQTT settings,
- Wi-Fi settings,
- alarmów.

---

# 19. Format API

API lokalne powinno być proste i przewidywalne.

Rekomendowana ścieżka:

```text
/api/...
```

Przykłady:

```text
/api/status
/api/settings
/api/network
/api/mqtt
/api/alarms
/api/diagnostics
/api/system/restart
```

Domenowe endpointy mogą być np.:

```text
/api/luma/...
/api/doser/...
/api/hydro/...
```

---

# 20. JSON w API

JSON jest dozwolony i preferowany dla lokalnego API WWW.

Zasady:

- krótkie klucze techniczne,
- brak zbędnych duplikatów,
- brak ogromnych odpowiedzi,
- tylko dane potrzebne danej stronie,
- brak przesyłania sekretów.

WWW i MQTT nie muszą używać identycznego formatu danych.

---

# 21. Sekrety

Credentials i sekrety MUSZĄ nigdy nie być zwracane przez Web API ani UI.

Dotyczy m.in.:

- hasła Wi-Fi,
- hasła MQTT,
- tokenów,
- kluczy.

UI może pokazać:

```text
********
```

lub stan:

```text
configured = true
```

Formularz MOŻE używać pustego inputu w znaczeniu „bez zmiany”, jeśli endpoint jawnie definiuje
tę semantykę. Nie wolno zwracać aktualnego hasła Wi-Fi, hasła MQTT, tokenu, hasła Basic Auth
ani secret key, także użytkownikowi uwierzytelnionemu.

---

# 22. Zapis konfiguracji

Zmiana konfiguracji z WWW ma być wykonywana według sekwencji:

```text
validate
-> save
-> apply
-> report result
```

Nie wolno zapisywać niepoprawnych danych tylko po to, aby potem wykrywać błąd.

Wyjątki, takie jak świadomie zaakceptowana błędna konfiguracja MQTT, są definiowane w odpowiednim standardzie.

---

# 23. Walidacja

Każde pole wejściowe musi mieć walidację po stronie urządzenia.

Walidacja w JavaScript jest tylko wygodą użytkownika i nie zastępuje backendu.

Backend musi sprawdzać:

- typ,
- zakres,
- długość,
- dozwolone wartości,
- format,
- zależności między polami.

---

# 24. Zasada atomic save

Konfiguracja grupy pól powinna być zapisywana atomowo tam, gdzie logicznie tworzą jeden zestaw.

Nie powinno dochodzić do stanu, w którym połowa nowej konfiguracji jest aktywna, a połowa stara.

Szczegóły mechanizmu określa `CONFIG_STORAGE_STANDARD`.

---

# 25. Apply bez restartu

Zmiana ustawienia powinna być stosowana bez restartu ESP, jeśli jest to technicznie bezpieczne.

Restart jest wymagany tylko wtedy, gdy naprawdę jest konieczny.

Przykłady zmian bez restartu:

- MQTT,
- HA Discovery,
- buzzer_enabled,
- większość ustawień domenowych,
- progi,
- harmonogramy,
- profile.

---

# 26. Restart-required

Jeśli dana zmiana wymaga restartu, WWW musi jasno to pokazać.

Przykład:

```text
Saved. Restart required.
```

Nie wykonujemy automatycznego restartu bez wiedzy użytkownika, chyba że akcja sama w sobie jednoznacznie oznacza restart/OTA/factory reset.

---

# 27. Feedback po akcji

Każda akcja użytkownika musi zakończyć się czytelnym wynikiem.

Przykłady:

```text
Saved
Applied
Connected
Failed
Invalid value
Authentication failed
Restarting
```

Nie wystarczy „kliknąć i nic nie pokazać”.

---

# 28. Błędy

Endpointy API zwracają canonical error envelope z sekcji
`Implementation: REQUIRED NEXT — W2 / Odpowiedzi i błędy`.

`code` MUSI być stabilne i machine-readable. `message` MOŻE zawierać krótki tekst dla
człowieka, a `field` MOŻE wskazywać pole. Odpowiedzi HTML i tekstowe poza API nie muszą używać
tej koperty.

Dla sukcesu:

```json
{
  "ok": true
}
```

Nie zwracamy ciężkich stack trace do UI.

---

# 29. Bezpieczne akcje destrukcyjne

Akcje wymagające potwierdzenia:

- factory reset,
- restore config,
- usunięcie konfiguracji,
- restart,
- OTA downgrade,
- inne nieodwracalne operacje.

Dla factory reset potwierdzenie powinno być wyraźniejsze niż zwykły klik.

---

# 30. Brak przypadkowych podwójnych akcji

Przyciski typu:

- restart,
- ACK ALL,
- manual dose,
- feed now,
- factory reset,

powinny być odporne na wielokrotne kliknięcie i duplikaty requestów.

Core / domena powinna zapewniać idempotencję lub blokadę ponownego wywołania tam, gdzie jest to potrzebne.

---

# 31. Stan strony a źródło prawdy

Frontend nie może zakładać, że operacja się udała tylko dlatego, że użytkownik kliknął przycisk.

Po zmianie konfiguracji lub stanu UI powinno odczytać faktyczny wynik z urządzenia.

Źródłem prawdy jest stan urządzenia.

---

# 32. Odświeżanie danych

Dla zwykłych danych wystarczy lekkie okresowe odpytywanie API.

Nie wymagamy WebSocket jako wspólnego standardu.

WebSocket/SSE może być używany tylko wtedy, gdy konkretny projekt naprawdę tego potrzebuje.

Preferencja:

```text
prosty polling > stałe połączenie
```

jeśli daje wystarczająco dobrą ergonomię.

---

# 33. Częstotliwość pollingu

Nie odświeżamy całego UI kilka razy na sekundę bez potrzeby.

Rekomendowane zakresy:

```text
status/dashboard: 2–5 s
diagnostics: 5–10 s
wolne dane: 10–30 s
```

Krytyczne akcje po kliknięciu mogą odświeżyć stan natychmiast.

---

# 34. Brak ciężkich wykresów jako standard

Core nie dostarcza rozbudowanego systemu wykresów/historycznych dashboardów.

Dane historyczne są domeną Home Assistant lub zewnętrznego systemu.

WWW służy do bieżącego stanu, konfiguracji i serwisu.

---

# 35. Home Assistant a WWW

HA i WWW mogą sterować tym samym stanem.

Przykłady:

- `buzzer_enabled`,
- mode,
- profile,
- ustawienia udostępnione do HA.

Zmiana z HA musi być widoczna w WWW po następnym odświeżeniu i odwrotnie.

Nie istnieją osobne kopie konfiguracji dla HA i WWW.

---

# 36. Konfiguracja techniczna tylko w WWW

Pełna konfiguracja techniczna pozostaje w WWW.

Przykłady:

- Wi-Fi,
- MQTT broker,
- MQTT credentials,
- HA Discovery prefix,
- kalibracje,
- ustawienia serwisowe,
- factory reset,
- OTA.

HA nie musi odwzorowywać całej konfiguracji urządzenia.

---

# 37. Diagnostyka tylko tam, gdzie potrzebna

Nie każda wartość widoczna w WWW musi być encją HA.

WWW może pokazywać znacznie więcej danych technicznych niż HA.

HA pozostaje czysty i użytkowy.

---

# 38. Techniczne nazwy

Core używa technicznych nazw i identyfikatorów.

WWW może prezentować czytelne etykiety, ale nie tworzy dodatkowej trwałej „friendly name” urządzenia.

Przyjazne nazwy urządzeń można nadawać w Home Assistant.

---

# 39. MQTT root w WWW

WWW pokazuje MQTT root jako read-only, zgodnie z `MQTT_STANDARD.md`.

Przykład:

```text
aquaone-A1B2C3/luma
```

Nie można go edytować.

---

# 40. MQTT settings

Sekcja MQTT zawiera co najmniej:

```text
MQTT enabled
broker
port
username
password
HA Discovery enabled
discovery_prefix
MQTT root (read-only)
Test MQTT
```

Zasady działania wynikają z `MQTT_STANDARD.md`.

---

# 41. Network settings

Sekcja sieci powinna zawierać tylko dane potrzebne urządzeniu.

Typowo:

```text
SSID
password
IP mode
optional static IP
```

Szczegółowy onboarding / AP / captive portal zostanie określony w `FACTORY_RESET_ONBOARDING_STANDARD`.

---

# 42. Time settings

Jeśli urządzenie używa czasu, WWW może pokazywać:

- aktualny czas,
- RTC status,
- NTP status,
- timezone.

Szczegółowy kontrakt czasu nie należy do WEB_STANDARD.

---

# 43. Mode

Wspólny mode:

```text
NORMAL
SERVICE
```

może być sterowany z WWW, jeśli ma to sens dla urządzenia.

Zmiana trybu przechodzi przez wspólną logikę Core/domeny.

---

# 44. safety_lock w WWW

Przy aktywnym `safety_lock` WWW:

- wyraźnie pokazuje blokadę,
- pokazuje przyczynę / alarmy blokujące,
- nie ukrywa stanu,
- blokuje lub oznacza niedostępne akcje zgodnie z `ALARM_STANDARD.md`.

---

# 45. Buzzer w WWW

WWW posiada wspólne ustawienie:

```text
buzzer_enabled
```

Steruje dokładnie tą samą trwałą wartością co HA.

---

# 46. Wersje

WWW w sekcji system/diagnostics pokazuje co najmniej:

```text
firmware_version
core_version
```

Jeśli istnieją:

```text
hardware_revision
config_schema_version
mqtt_protocol_version
```

również mogą być pokazane.

---

# 47. Restart

WWW udostępnia kontrolowany restart.

Sekwencja restartu musi respektować:

- MQTT availability offline i controlled disconnect, jeśli projekt ma aktywny transport
  MQTT implementujący ten lifecycle,
- zapis konfiguracji,
- bezpieczne zatrzymanie domeny.

Szczegóły wynikają z innych standardów.

---

# 48. Factory reset

WWW może udostępniać factory reset w sekcji System.

Akcja:
- wymaga jednoznacznego potwierdzenia,
- nie może być łatwa do przypadkowego wykonania,
- zachowanie szczegółowe definiuje `FACTORY_RESET_ONBOARDING_STANDARD`.

---

# 49. OTA

WWW może udostępniać OTA.

OTA nie powinno być częścią zwykłych ustawień.

Szczegóły bezpieczeństwa, rollback i walidacja firmware są definiowane w `OTA_STANDARD.md`.

---

# 50. Backup / restore

Jeśli urządzenie wspiera eksport/import konfiguracji, funkcja jest dostępna w System/Settings.

Format i kompatybilność definiuje `CONFIG_STORAGE_STANDARD`.

---

# 51. Dostęp do WWW

WWW jest lokalnym interfejsem urządzenia.

Standard nie wymaga chmurowego zdalnego dostępu do ESP.

Jeśli użytkownik chce dostęp zdalny, powinien odbywać się przez bezpieczną infrastrukturę sieciową, a nie przez wystawianie niezabezpieczonego ESP bezpośrednio do Internetu.

---

# 52. Autoryzacja WWW

WEB_STANDARD nie wymusza ciężkiego systemu kont użytkowników.

Architektura Core powinna pozwolić na lekką ochronę dostępu, jeśli będzie wymagana przez `SAFETY_STANDARD` lub przyszły standard security.

Nie projektujemy rozbudowanego systemu sesji bez realnej potrzeby.

---

# 53. Nie logujemy sekretów

WWW i backend nie mogą logować:

- haseł Wi-Fi,
- haseł MQTT,
- tokenów,
- danych uwierzytelniających.

Dotyczy logów normalnych i diagnostycznych.

---

# 54. Stabilność przy błędach WWW

Błąd HTTP, błędny request lub zamknięta przeglądarka nie mogą:

- zatrzymać domeny,
- zablokować loop,
- zrestartować urządzenia,
- destabilizować działania autonomicznego.

WebService jest usługą pomocniczą, nie centrum działania urządzenia.

---

# 55. Ograniczenie pamięci

Core powinien unikać:

- dużych Stringów tworzonych wielokrotnie,
- bardzo dużych jednorazowych buforów,
- dynamicznego generowania ogromnego HTML,
- duplikacji całej strony w RAM.

Preferowane są:
- stałe teksty w flash,
- małe bufory,
- odpowiedzi strumieniowane/chunked tam, gdzie ma to sens,
- współdzielony HTML shell.

---

# 56. Statyczne zasoby

CSS/JS powinny być współdzielone przez Core, jeśli to możliwe.

Projekt domenowy dodaje tylko własne fragmenty potrzebne do konkretnej strony.

Nie kopiujemy tego samego CSS/JS do każdego projektu.

---

# 57. Rejestr stron

Core powinien umożliwiać rejestrację stron przez projekt domenowy.

Przykład logiczny:

```text
registerPage("Dashboard", ...)
registerPage("Control", ...)
registerPage("Settings", ...)
```

Dokładne API może być inne, ale projekt nie powinien modyfikować wewnętrznego routera Core.

---

# 58. Rejestr API

Projekt domenowy powinien rejestrować własne endpointy przez wspólne API Web Core.

Core odpowiada za:
- routing,
- błędy,
- JSON helpers,
- wspólne nagłówki,
- walidację bazową.

Domena odpowiada za znaczenie requestu.

---

# 59. Endpointy systemowe Core

Core powinien docelowo dostarczyć wspólne endpointy dla:

```text
status
diagnostics
alarms
network
mqtt
system
versions
```

Projekt domenowy nie powinien kopiować tych endpointów.

---

# 60. Status HTTP

API powinno używać sensownych kodów HTTP.

Przykładowo:

```text
200 OK
400 Bad Request
401/403 Unauthorized/Forbidden, jeśli ochrona jest aktywna
404 Not Found
409 Conflict
500 Internal Server Error
503 Service Unavailable
```

Nie zwracamy zawsze `200` dla każdego błędu.

---

# 61. Walidacja request size

Core powinien ograniczać maksymalny rozmiar requestów i payloadów API.

Nie przyjmujemy nieograniczonych JSON-ów.

Dokładne limity zależą od endpointu i zasobów urządzenia.

---

# 62. Formularze

Formularze:
- pokazują aktualne wartości,
- mają jednostki,
- pokazują min/max, jeśli istnieją,
- używają sensownych input types,
- nie zapisują automatycznie przy każdej zmianie suwaka, jeśli to powoduje zbędne zapisy flash.

Preferowany model:

```text
edit
-> Save
-> validate
-> apply
```

---

# 63. Dane chwilowe vs trwałe

UI powinno rozróżniać:
- ustawienia trwałe,
- chwilowe akcje,
- aktualny stan.

Przykład:
- `buzzer_enabled` = trwała konfiguracja,
- `ACK ALL` = akcja,
- `alarm_active` = stan.

Nie mieszamy tych pojęć w jednym modelu.

---

# 64. Brak autosave dla konfiguracji

Domyślnie ustawienia trwałe nie są zapisywane przy każdej zmianie pojedynczego pola.

Użytkownik edytuje zestaw i naciska `Save`.

Wyjątkiem mogą być proste przełączniki, dla których natychmiastowy zapis jest naturalny, np. `buzzer_enabled`.

---

# 65. Odporność na odświeżenie strony

Odświeżenie lub ponowne otwarcie WWW nie może:
- ponawiać akcji destrukcyjnej,
- ponawiać manualnego sterowania,
- wykonywać ostatniego POST ponownie bez intencji użytkownika.

Akcje powinny być projektowane tak, aby refresh był bezpieczny.

---

# 66. Brak zależności od kolejności stron

Każda strona WWW pobiera aktualny stan z urządzenia.

Nie zakładamy, że użytkownik wcześniej odwiedził inną stronę lub że frontend posiada komplet aktualnego stanu.

---

# 67. Lokalizacja językowa

Techniczne identyfikatory pozostają po angielsku.

Teksty UI mogą być po polsku lub w przyszłości lokalizowane.

Core nie powinien mieszać technicznych kluczy z tłumaczeniami.

Przykład:

```text
alarm_id = water_level_low
UI label = Niski poziom wody
```

---

# 68. Dostępność UI

Interfejs powinien być czytelny bez polegania wyłącznie na kolorze.

Statusy mają mieć również tekst/ikonę, np.:

```text
OK
WARNING
ERROR
CRITICAL
```

---

# 69. Wspólne kolory semantyczne

UI może używać wspólnych semantycznych stylów:

```text
normal
info
warning
error
critical
disabled
```

Dokładne kolory należą do wspólnego theme Core.

Projekt domenowy nie powinien tworzyć własnej konkurencyjnej semantyki kolorów.

---

# 70. Ikony

Ikony są opcjonalne.

Nie powinny być wymagane do zrozumienia stanu.

Preferowane są lekkie lokalne SVG lub proste symbole, jeśli nie zwiększają znacząco rozmiaru firmware.

---

# 71. Wersjonowanie WWW

WWW nie ma osobnej niezależnej wersji protokołu, jeśli nie ma takiej potrzeby.

Interfejs WWW jest częścią firmware urządzenia.

API może być rozwijane kompatybilnie; wersjonowanie API wprowadzamy dopiero wtedy, gdy realnie pojawi się potrzeba wielu klientów zewnętrznych.

---

# 72. Brak publicznego API jako celu v1

Lokalne API WWW jest przede wszystkim backendem dla własnego UI urządzenia.

Nie traktujemy go automatycznie jako publicznego API dla integracji zewnętrznych.

Integracje zewnętrzne używają przede wszystkim MQTT/HA.

---

# 73. Testowalność

Warstwa Web Core powinna być testowalna bez fizycznego hardware.

Należy testować co najmniej:
- routing,
- walidację,
- format błędów,
- zapis konfiguracji,
- akcje systemowe,
- blokadę przez safety_lock,
- brak wycieku sekretów.

---

# 74. Granica Core / domena

Core dostarcza techniczną infrastrukturę:
- jeden fizyczny HTTP server przez backend,
- `WebService` i routing primitives,
- request/response oraz auth primitives,
- upload lifecycle START/CHUNK/END/ABORT,
- wspólne strony i zasoby, które są faktycznie zaimplementowane.

Domena dostarcza:
- własne strony,
- własne pola,
- własne endpointy,
- własne command callbacks,
- własne dane,
- politykę operacji i ich efekty.

W Doserze lokalny `WebManager` jest poprawnym koordynatorem polityki domenowej: posiada
politykę restartu i OTA, zatrzymanie pomp, maintenance, cleanup oraz restart sequencing.
Korzysta z jednego `WebService`/`Esp32WebBackend` i nie posiada drugiego serwera HTTP.

---

# 75. Nie duplikujemy infrastruktury

Projekt NIE MOŻE tworzyć:
- drugiego fizycznego `WebServer`,
- drugiego transportu HTTP,
- konkurencyjnego routera technicznego.

Projekt MOŻE i często powinien zachować lokalny koordynator polityki domenowej typu
`WebManager`. Sama nazwa klasy nie jest powodem do jej usunięcia. Wspólna infrastruktura
techniczna pozostaje własnością `aquaOneCore`, a polityka produktu pozostaje w domenie.

---

# 76. Migracja istniejących projektów

Istniejące projekty mogą zachować własne strony domenowe.

Podczas migracji do Core:
1. najpierw przenosimy wspólną infrastrukturę,
2. potem wspólne strony systemowe,
3. na końcu domenowe strony korzystają z nowego Core.

Nie przepisujemy całego UI naraz bez potrzeby.

---

# 77. Zasada końcowa

WWW ma być prostym, lokalnym i niezawodnym interfejsem serwisowo-konfiguracyjnym urządzenia.

Nie powinno być ciężkim frontendem ani drugim Home Assistantem.

**Core zapewnia wspólny szkielet. Domena dostarcza tylko to, co specyficzne dla urządzenia.**
