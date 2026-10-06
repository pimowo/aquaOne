# LumaSense — WWW po F9.7D

## 1. Stan i granice

F9.7D przełącza produkcyjne WWW na jeden `EspIdfWebTransport` używany przez
`NativeWebService`. Nie powstaje legacy `Esp32WebBackend` ani `WebService`, a
transport HTTPD nie wymaga `update()`/`handleClient()`. WWW pozostaje opcjonalnym
interfejsem lokalnym. `FirmwareApp` uruchamia się przed siecią, a
`FirmwareApp::update()` pozostaje pierwszą operacją pętli. Jest to przejściowa
kompozycja produktu, bez migracji całej Lumy do `ApplicationRuntime`.

Brak Wi-Fi, rozłączenie albo błąd Web nie zatrzymują Core. HTTPD czyta wyłącznie
opublikowane snapshoty i nie posiada `FirmwareApp`, Network, Diagnostics,
Storage ani Hardware. Mutacje przechodzą jako owned `LumaWebRequest` przez
stałopojemnościowy `WebApplicationBridge`; najwyżej jedno żądanie wykonuje się
w Application loop. Profil pozostaje `CONFIG_ACTION`, a mode/manual są
przejściową serializowaną ścieżką Application. Manual używa bieżącego `nowMs`
Application, nie czasu HTTPD.

## 2. Strony

| Trasa | Zawartość |
|---|---|
| `/` | Dashboard: profil, tryb, DAY/NIGHT, czas, ważność czasu, limit, Wi-Fi, IP, RSSI, health oraz osiem kanałów requested/final. |
| `/control` | Profil 1–5, NORMAL/SERVICE/OFF i MANUAL dla ośmiu kanałów z timeoutem 0/15/30/60 minut. |
| `/diagnostics` | Widok istniejącego `/api/diagnostics`. |
| `/system` | Widok istniejącego `/api/system`. |

Strony używają wspólnego `HtmlShell` i lokalnego `/assets/aqua.css`. Nie pobierają zasobów z Internetu. Dashboard odpytuje status co 1500 ms; polling jest read-only.

## 3. API

| Metoda i trasa | Kontrakt |
|---|---|
| `GET /api/lumasense/status` | mode, profil, DAY/NIGHT, czas, requested/final, limit, timeValid, Wi-Fi i health. |
| `POST /api/lumasense/mode` | `{"mode":"NORMAL|SERVICE|OFF|EXIT_MANUAL"}`. |
| `POST /api/lumasense/profile` | `{"profile":1..5}`; zapis Storage przed aktywacją. |
| `POST /api/lumasense/manual` | osiem poziomów 0..100 i timeout 0/15/30/60. |

Body POST ma limit 512 bajtów. Parser odrzuca niepełny lub rozszerzony schemat, niepoprawne typy, NaN/Inf, zakresy i dane po obiekcie. Złe dane dają 400, konflikt stanu lub zapis Storage 409, a idempotentna operacja poprawne `no_change`. Dynamiczne wartości są escapowane dla JSON/HTML.

Oczekiwanie HTTPD na wynik jest ograniczone do 250 ms. Jest to prowizoryczna
wartość integracyjna Lumy, nie standard Core. F9.7D2 HIL dał 20 bezpiecznych
POST no-change bez 202; mediana wyniosła 249.802 ms, p95 504.565 ms, a maksimum
561.396 ms. Szczegóły i dalsze ryzyko timingowe opisuje
`docs/WEB_F9_7D2_LUMA_HIL.md`.
Queue-full lub awaria przed acceptance daje 503. Timeout po acceptance daje
202 z `outcome_unknown`; nie anuluje, nie ponawia i nie wysyła ponownie żądania.
ControlPage sprawdza `body.ok`, więc 202 nie jest prezentowane jako sukces.

## 4. Trwałość i restart

Zmiana profilu tworzy kopię `DeviceConfig`, waliduje ją, zapisuje transakcyjnym Storage i dopiero po sukcesie aktywuje. `activeProfileIndex` wraca po restarcie. Tryby runtime nie są zapisywane; restart zawsze uruchamia NORMAL i nie odtwarza MANUAL/OFF.

## 5. Sieć i sekrety

Repo zawiera tylko `include/NetworkSecrets.example.h` z wyłączonym Wi-Fi i pustymi polami. Lokalny `include/NetworkSecrets.h` jest ignorowany. Hasło nie trafia do logu, stron ani API. Diagnostyka może podać SSID.

Luma używa STA z reconnectem co 10 s i wyłączonym SoftAP. HTTP nie ma
uwierzytelniania ani HTTPS; jest to zachowanie kompatybilności CURRENT, a nie
globalna decyzja WEB-101. WEB-101 nadal wymaga decyzji, a SEC-101 pozostaje OPEN.

Dashboard zachowuje polling statusu co 1500 ms. Luma nie konfiguruje WebSocket,
Realtime ani StreamStart w F9.7D.

F9.7D ma real HTTP/Wi-Fi HIL PASS na `LOLIN32_TEST`: jeden listener portu 80,
natywne strony/API, 512/513-byte body boundary, Application actions i 60 s
stability soak. F9.7D jest CURRENT/CLOSED po checkpointcie. Nie zamyka to
Phase 9 ani nie zmienia otwartego WEB-101 i SEC-101.

## 6. F9.7D2 HIL

F9.7D2 completed real production HTTP/Wi-Fi HIL on `LOLIN32_TEST` using one
`EspIdfWebTransport` on port 80. Native pages and APIs, the 512/513-byte body
boundary, Application actions, profile restore, a partial-body disconnect and a
60-second light soak passed. The sanitized evidence is
`docs/WEB_F9_7D2_LUMA_HIL.md`.

The 250 ms Application wait remains a provisional product value. Twenty safe
no-change POST requests produced no 202 response; host-observed median was
249.802 ms, p95 504.565 ms and maximum 561.396 ms. The tail latency remains a
residual scheduling/HTTPD risk. Luma remains polling-only; WEB-101 is DECISION
REQUIRED and SEC-101 remains OPEN.

## 7. Poza AC8

AC8 nie dodaje CHANNEL_TEST, PREVIEW, SIMULATION, edytora profili/kanałów, konfiguratora sieci, NTP, MQTT, Home Assistant, OTA ani captive portalu.


## 7. Redesign wizualny oparty na yoPILOT

Po AC8 warstwa wizualna została dostosowana do projektu referencyjnego yoPILOT z `reference/src/ConfigPortal.cpp`. Wspólny theme używa jego rzeczywistych wartości: szerokość 720 px, tło `#101827`, powierzchnie `#1f2937` i `#111827`, tekst `#e5e7eb`, muted `#9ca3af`, akcent `#65d46e`, granice `#374151/#4b5563` oraz breakpoint 460 px.

Do Aqua Core trafiły wyłącznie wspólne prymitywy: shell, zmienne CSS, header, aktywna nawigacja, karty, wiersze key/value, badge, przyciski, formularze, stany i responsywność. LumaSense zachowuje układ danych Dashboard, kanałów, trybów, MANUAL, Diagnostics i System.

Dashboard pokazuje requested i final dla wszystkich kanałów. Diagnostics i System prezentują wszystkie istniejące dane w kartach. Control zachowuje profile 1–5, tryby, osiem suwaków i timeout 0/15/30/60. API, polling 1500 ms i semantyka Core nie zmieniły się. Interfejs pozostaje całkowicie offline i bez frameworka frontendowego.

Wersje po redesignie: LumaSense **0.2.1**, Aqua Core **0.6.2**.
