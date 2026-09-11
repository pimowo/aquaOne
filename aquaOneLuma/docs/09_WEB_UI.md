# LumaSense — WWW po AC8

## 1. Stan i granice

AC8 podłącza istniejące moduły `AquaCore::Network`, `AquaCore::Diagnostics` i `AquaCore::Web` do produkcyjnego `main.cpp`. WWW jest opcjonalnym interfejsem lokalnym. `FirmwareApp` uruchamia się przed siecią, a `FirmwareApp::update()` pozostaje pierwszą operacją pętli.

Brak Wi-Fi, rozłączenie albo błąd Web nie zatrzymują Core. Kod `src/web` nie zapisuje GPIO/PWM i nie odwołuje się do `HardwareInterface` ani `TransitionEngine`. Polecenia przechodzą przez ścisłą walidację, `FirmwareApp`, istniejące API `LumaCore` i — dla profilu — istniejący `StorageService`.

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

## 4. Trwałość i restart

Zmiana profilu tworzy kopię `DeviceConfig`, waliduje ją, zapisuje transakcyjnym Storage i dopiero po sukcesie aktywuje. `activeProfileIndex` wraca po restarcie. Tryby runtime nie są zapisywane; restart zawsze uruchamia NORMAL i nie odtwarza MANUAL/OFF.

## 5. Sieć i sekrety

Repo zawiera tylko `include/NetworkSecrets.example.h` z wyłączonym Wi-Fi i pustymi polami. Lokalny `include/NetworkSecrets.h` jest ignorowany. Hasło nie trafia do logu, stron ani API. Diagnostyka może podać SSID.

AC8 używa STA z reconnectem co 10 s i wyłączonym SoftAP. HTTP nie ma uwierzytelniania ani HTTPS, więc sterowanie jest przeznaczone wyłącznie dla zaufanej sieci LAN.

## 6. Poza AC8

AC8 nie dodaje CHANNEL_TEST, PREVIEW, SIMULATION, edytora profili/kanałów, konfiguratora sieci, NTP, MQTT, Home Assistant, OTA ani captive portalu.


## 7. Redesign wizualny oparty na yoPILOT

Po AC8 warstwa wizualna została dostosowana do projektu referencyjnego yoPILOT z `reference/src/ConfigPortal.cpp`. Wspólny theme używa jego rzeczywistych wartości: szerokość 720 px, tło `#101827`, powierzchnie `#1f2937` i `#111827`, tekst `#e5e7eb`, muted `#9ca3af`, akcent `#65d46e`, granice `#374151/#4b5563` oraz breakpoint 460 px.

Do Aqua Core trafiły wyłącznie wspólne prymitywy: shell, zmienne CSS, header, aktywna nawigacja, karty, wiersze key/value, badge, przyciski, formularze, stany i responsywność. LumaSense zachowuje układ danych Dashboard, kanałów, trybów, MANUAL, Diagnostics i System.

Dashboard pokazuje requested i final dla wszystkich kanałów. Diagnostics i System prezentują wszystkie istniejące dane w kartach. Control zachowuje profile 1–5, tryby, osiem suwaków i timeout 0/15/30/60. API, polling 1500 ms i semantyka Core nie zmieniły się. Interfejs pozostaje całkowicie offline i bez frameworka frontendowego.

Wersje po redesignie: LumaSense **0.2.1**, Aqua Core **0.6.2**.
