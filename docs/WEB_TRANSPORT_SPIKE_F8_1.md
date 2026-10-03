# F8.1 — HTTP + WebSocket feasibility spike

Status: **NOT FEASIBLE WITH CURRENT STACK**. To wynik auditu istniejącego stacku,
bez wyboru zamiennika i bez zmiany produkcyjnego Web. Wymóg WEB-001 — jeden fizyczny
server i port dla HTTP oraz WebSocket — pozostaje TARGET; Phase 9 nie może przyjąć
obecnego `WebServer` jako gotowego backendu realtime.
Forma spike: izolowany audit API/implementacji i compile-only Core HTTP. Nie dodano
fixture udającej WebSocket, ponieważ nie mogłaby dowieść współdziałania na obecnym backendzie.

## Stan CURRENT

`AquaCore::Web::Esp32WebBackend` posiada jeden Arduino `WebServer(port)` na instancję
`WebService`; rejestruje trasy, wywołuje `begin()` oraz pojedyncze `handleClient()` w
`update()`. Core nie ma WebSocket servera ani biblioteki WS w aktywnych `platformio.ini`.
Luma i Doser tworzą po jednym backendzie jako obiekty o statycznym czasie życia;
Hydro ma jeden backend będący polem `HydroSenseApp`. Konfigurują HTTP na porcie 80.
Trasy produktu, w tym Doser
restart/OTA, Hydro `/api/settings`, Luma status/control oraz Core
`/api/system` i `/api/diagnostics`, trafiają do wspólnego `WebService` danego
urządzenia. Nie znaleziono aktywnej Domain tworzącej drugi HTTP/WS server.

Zainstalowane warianty Arduino ESP32 `WebServer.h` deklarują obsługę jednego klienta
naraz i nie wystawiają API upgrade, ramek ani callbacków WebSocket. `handleClient()`
trzyma jeden `_currentClient` i nie przyjmuje następnego klienta, gdy obsługuje bieżący.
Przejęcie tego klienta na trwałe WS uniemożliwiałoby równoległy GET na tym samym
serwerze. Dodanie osobnego WS listenera spełniłoby tylko zasadę jednego właściciela,
lecz złamałoby dosłowny wymóg jednego fizycznego servera/portu. Tego wariantu nie
wdrożono ani nie uznano za rozstrzygnięcie WEB-001.
Dowód: zainstalowane `framework-arduinoespressif32` 2.0.17 dla Core oraz wariant 3.1.3
użyty w buildzie Luma; `libraries/WebServer/src/WebServer.h`, `WebServer.cpp` i
`Parsing.cpp`, a w repo `aquaOneCore/src/Web/Esp32WebBackend.cpp`.

| Pytanie | Wynik F8.1 |
| --- | --- |
| Jeden owner dla istniejącego HTTP i tras Core/Application | Tak, `WebService` i jeden backend na urządzenie. |
| HTTP i WS równolegle na aktualnym backendzie i porcie 80 | Nie: brak WS API i tylko jeden aktywny klient HTTP. |
| Ograniczony czas servicing w Application loop | Nieudowodniony; obecny parser może czekać na payload. |
| Trasy Core i produktu bez dodatkowego servera | Tak dla HTTP; obecny routing jest ograniczony, nie jest finalnym API Domain. |
| HTTP snapshot jako source of truth, WS jako live delta | Zgodne z architekturą, bez wykonanego proof WS. |
| WS reconnect → pełny HTTP snapshot | Koncepcyjnie możliwe, lecz niepotwierdzone na obecnym backendzie. |

## Granice biblioteki i obserwacje

- Brak ruchu: `handleClient()` wraca po `delay(1)` przy domyślnym `_nullDelay`; nie
  wykonano pomiaru czasu ticku na urządzeniu.
- Parser requestu używa `readStringUntil()`; zwykły POST odczytuje całe `Content-Length`
  do bufora `malloc/realloc` i `String("plain")`, czekając do `HTTP_MAX_POST_WAIT = 5000 ms`
  na każdy okres bez danych. Aktualny Core `WebRouteOptions::maxBodyLength` domyślnie wynosi 0
  (brak limitu); kontrola długości w backendzie następuje podczas dispatch, już po
  parsowaniu body przez bibliotekę. Nie jest więc limitem pamięci parsera.
- Multipart upload ma callbacki porcjami; domyślnie `HTTP_UPLOAD_BUFLEN` wynosi 1436 B,
  ale jest makrem kompilacji i może zostać nadpisane. Osobne pola formularza i request
  headers nadal używają dynamicznego `String`.
  Limity body, chunking, timeout i backpressure dla Config/OTA/Restore wymagają
  osobnego kontraktu i pomiaru na wybranym backendzie.
- WS text/binary, fragmenty ramek, limit payloadu, broadcast i liczba klientów WS:
  **nie dotyczy obecnego stacku**. Nie wolno z tego wywodzić wsparcia dla wielu klientów.
- Zachowanie dla slow client, reconnect WS oraz równoległego HTTP+WS:
  **UNKNOWN / NOT MEASURED**.
- Biblioteka i adapter HTTP używają heap (`WebServer`, handlery, `String`, bufor POST).
  Nie zdefiniowano dynamicznej rejestracji jako wymogu kontraktu vNext. Flash delta,
  static RAM oraz runtime heap HTTP+WS pozostają **niezmierzone**, ponieważ nie powstał
  porównywalny firmware HTTP+WS. Nie należy przypisywać tym wielkościom zera.

Nie dodano `/api/spike/snapshot`, `/ws/spike` ani POST/action: na obecnym stacku
nie byłyby dowodem współdziałania HTTP+WS. Istniejący HTTP ma routing i odpowiedzi
request/response, ale test 200/400/404 razem z WS connect/message/disconnect,
równoległym GET, reconnect i network-loss **nie został wykonany**. Transport nie
powinien modyfikować `OperationalState` ani Safety; snapshot ma pozostać autorytatywny,
a WS ma później jedynie informować o zmianie. HTTP action musi przejść przez
istniejącą granicę Commands. Dispatch route nie stanowi autoryzacji; Auth, TLS,
handshake, limity klientów i reprezentacja payloadu pozostają otwarte. CURRENT HTTP
jest plaintext na porcie 80; przyszły WS bez TLS byłby `ws://`.

Network jest opcjonalny. Transport może być potrzebny w Maintenance i nie powinien
przechodzić przez `NormalProcessing` gate. Prerequisite to uruchomiona infrastruktura
Network, bez oczekiwania na klienta lub Internet. Po utracie i odzyskaniu STA trzeba
sprawdzić dostępność HTTP/WS na fizycznym urządzeniu; nie przeprowadzono takiego HIL.

## Walidacja i dalsza bramka

- Native baseline: **368/368 PASS** w 11 zaakceptowanych suites; bez nowego native
  fake WebServer i bez zmian kodu produkcyjnego.
- ESP32 compile-only `test_system`: **PASS**; build zawiera obiekt
  `src/Web/Esp32WebBackend.cpp.o`, więc sprawdza kompilację istniejącego HTTP Core.
  To **nie** jest compile proof HTTP+WS. Legacy `test_web` i build Luma kończą się
  wcześniejszą niezgodnością fixture/adaptera `StorageService` (4 argumenty wobec 5),
  niezwiązaną z F8.1.
- HIL: **NOT RUN**. Po wyborze kandydata: boot, Wi-Fi, GET pełnego snapshotu,
  WS connect/event, równoległy GET, WS disconnect/reconnect i ponowny GET; dodatkowo
  200/400/404, WS message/disconnect, slow client, POST body, network-loss,
  Maintenance, czas ticku oraz flash/static RAM/heap względem porównywalnego baseline.

WEB-103 pozostaje DECISION REQUIRED. F8.2 ma wybrać i porównać kandydatów, którzy
obsłużą HTTP+WS na jednym listenerze/porcie, więcej niż jedno aktywne połączenie,
równoległy GET przy aktywnym WS, praktyczne bounded servicing, kontrolę request body
i określone WS fragmentation semantics. Należy zmierzyć firmware/flash delta,
RAM/static usage, runtime heap, liczbę klientów WS oraz limity body i frame na ESP32.
F8.2 nie migruje production Web.
Bundled ESP-IDF `esp_http_server` ma API WS w zainstalowanym SDK, ale F8.1 **nie wybiera**
go ani nie migruje obecnego Web. Phase 9 na obecnym stacku nie może jeszcze rozpocząć
produkcyjnej implementacji wspólnego HTTP+Realtime.
