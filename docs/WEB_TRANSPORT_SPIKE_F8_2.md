# F8.2 — Web transport backend candidate comparison

Status: **CANDIDATES COMPILE — HIL REQUIRED**. F8.2 porównuje dwa backendy na
CURRENT platformie i nie wybiera jeszcze backendu produkcyjnego. Oba kandydaty
kompilują jeden fizyczny server/listener na porcie 80 z HTTP i WebSocket, lecz bez
testu na urządzeniu nie ma dowodu najważniejszej własności WEB-001: poprawnego GET
drugiego klienta przy utrzymywanym WS oraz zachowania pod obciążeniem.

Spike jest odizolowany w `spikes/web_transport_f8_2`. Nie zmienia
`Esp32WebBackend`, `WebBackend`, `WebService`, composition produktów ani platformy
produkcyjnej. Każdy wariant uruchamia jeden application-owned transport i ten sam
proof:

- otwarty AP `aquaone-f8-spike`, domyślnie `192.168.4.1`;
- `GET /api/spike/snapshot` → `{"revision":1,"value":42}`;
- WebSocket `/ws/spike` → po connect
  `{"type":"spike.changed","revision":1}`;
- tekst odebrany z WS wyzwala broadcast tego samego zdarzenia;
- snapshot HTTP jest pełnym source of truth, a WS tylko powiadomieniem live.

To nie jest finalny envelope, API, Auth ani realtime protocol.

## Dokładne wersje i metoda

| Element | Wersja użyta w buildzie |
| --- | --- |
| PlatformIO Core | 6.2.0 |
| PlatformIO platform | `platformio/espressif32@6.13.0` |
| Board | `esp32dev` — ESP32 Dev Module |
| Arduino-ESP32 | 2.0.17 (`framework-arduinoespressif32@3.20017.241212+sha.dcc1105b`) |
| ESP-IDF base | 4.4.7 |
| Async HTTP/WS | `ESP32Async/ESPAsyncWebServer@3.6.0` |
| Async TCP | `ESP32Async/AsyncTCP@3.3.2` |

Wersje są przypięte w spike `platformio.ini`; nie użyto archiwalnego forka
`me-no-dev`. PsychicHttp nie był badany: jest opcjonalny, a obaj obowiązkowi
kandydaci przeszli compile proof; dodanie trzeciej zależności nie rozstrzygnęłoby
brakującego HIL i nie było uzasadnione.

Zbudowano ten sam board, framework, domyślne release flags, Wi-Fi AP i instrumentację
heap. `web_spike_baseline` nie zawiera serwera, snapshot literal ani eventu; jest
transport-free AP baseline, a nie firmware o identycznej funkcjonalności. Kandydaci
mają ten sam snapshot i event, ale IDF zawiera dodatkowo bounded POST, którego wariant
Async nie implementuje. Deltę można więc traktować jako koszt całego konkretnego
spike ponad wspólny AP baseline, nie jako precyzyjną izolację kosztu samej biblioteki
ani idealne porównanie like-for-like. Polecenie:

```text
platformio run -d spikes/web_transport_f8_2 -e web_spike_baseline -e web_spike_async -e web_spike_idf
```

Wszystkie trzy środowiska: **COMPILE/LINK PASS**.

## Kandydat A — ESP32Async

`web_spike_async` ma jedną `AsyncWebServer(80)` i dołącza
`AsyncWebSocket("/ws/spike")` jako handler tego samego serwera. Nie powstaje drugi
listener ani port. AsyncTCP obsługuje wiele połączeń zdarzeniowo, więc architektura
nie rezerwuje listenera dla jednego trwałego WS; rzeczywista równoległość z HTTP,
liczba klientów i odporność na slow client wymagają HIL.

Źródła dokładnie przypiętych wersji pokazują:

- callbacki HTTP/WS wykonują się z obsługi zdarzeń AsyncTCP, poza Arduino `loop()`;
  sieć nie wymaga service call w `loop()`. Spike wywołuje `cleanupClients()` tylko
  do usuwania rozłączonych klientów i zastosowania domyślnego limitu cleanup;
- AsyncTCP używa konfigurowalnych compile-time defaults: task `async_tcp`, core `-1`
  (nieprzypięty), priority 10, stack 16384 B, event queue 64 i WDT włączony. Są to
  makra `CONFIG_ASYNC_TCP_*`, które spike dziedziczy bez nadpisania; nie są hard API
  limits;
- event packets, obiekty request/client, callbacki `std::function`, `String`, STL
  containers i współdzielone bufory wiadomości korzystają wewnętrznie z heap;
- library default argument `DEFAULT_MAX_WS_CLIENTS` na ESP32 wynosi 8 i taki argument
  stosuje wywołanie `cleanupClients()` w spike. Caller może podać inną wartość; nie
  jest to hard API limit ani zmierzona pojemność produkcyjna. lwIP w tym SDK ma
  `CONFIG_LWIP_MAX_ACTIVE_TCP=16`;
- per-client WS message queue używa konfigurowalnego compile-time defaultu
  `WS_MAX_QUEUED_MESSAGES=32`; to również nie jest hard API limit. Domyślnie pełna
  kolejka zamyka klienta; można jawnie przełączyć zachowanie na odrzucanie nowej wiadomości.
  `queueIsFull()`/`availableForWrite*()` pozwalają sprawdzić backpressure, a broadcast
  zwraca `DISCARDED`, `ENQUEUED` albo `PARTIALLY_ENQUEUED`;
- są osobne API text/binary i broadcast. Ping generuje event i automatyczny pong,
  close/pong/error/disconnect są widoczne w callbackach;
- receive może dostarczać frame porcjami. `AwsFrameInfo` niesie `message_opcode`,
  numer fragmentu, `final`, aktualny `opcode`, długość frame i `index` porcji. Send
  dzieli wiadomość według dostępnego okna TCP, używając continuation i FIN;
- zwykłe raw body może trafiać porcjami do `onBody(data, len, index, total)`, więc
  application może sprawdzić `total` przy pierwszej porcji i nie składać całego
  body. Biblioteka nie daje jednak w tym proofie osobnej, przetestowanej gwarancji
  hard reject przed pierwszą porcją. Form-urlencoded i nieplikowe multipart fields
  używają dynamicznych `String`; multipart file callback w tej wersji używa porcji z
  compile-time implementation default `RESPONSE_STREAM_BUFFER_SIZE=1460`. Ten rozmiar
  może zostać nadpisany i nie jest gwarancją publicznego API. Async body 400/413 nie
  był wykonywany;
- request i headers są dostępne przy handshake, a middleware/auth może poprzedzić
  akceptację WS. Auth nie został zaimplementowany. Ścieżki TLS nie oceniano.

Callback poza głównym taskiem oznacza, że Phase 9 musi ustanowić jawny cross-task
boundary; handler nie może bezpośrednio mutować Domain bez ustalonej serializacji.
Core contract nadal może być bounded i zero-dynamic, niezależnie od alokacji biblioteki.

## Kandydat B — ESP-IDF `esp_http_server`

`web_spike_idf` używa jednego `httpd_handle_t` z `server_port=80` oraz trzech URI:
HTTP snapshot, bounded POST i WS. `CONFIG_HTTPD_WS_SUPPORT=y` jest aktywne w
zainstalowanym frameworku; nagłówki, handshake, `httpd_ws_recv_frame`,
`httpd_ws_send_frame`, `httpd_ws_send_frame_async`, `httpd_ws_send_data_async`,
`httpd_ws_get_fd_info`, `httpd_get_client_list` i `httpd_queue_work` są dostępne i
przeszły compile/link. Nie potrzeba przebudowy frameworka ani `sdkconfig`.

Spike jawnie ustawia tylko `config.server_port=80` po inicjalizacji
`HTTPD_DEFAULT_CONFIG()`; pozostałe poniższe wartości są odziedziczonymi library
defaults i jednocześnie rzeczywistymi wartościami config tego builda:

- jeden server task, priority `tskIDLE_PRIORITY + 5`, stack 4096 B, core
  `tskNO_AFFINITY`;
- `max_open_sockets=7`, backlog 5, `max_uri_handlers=8`, LRU purge wyłączone;
- receive i send timeout po 5 s; globalny lwIP limit socketów wynosi 16;
- persistent WS zajmuje jeden z siedmiu client sockets. Liczby 1 WS, 2 WS i HTTP
  przy obu mieszczą się w konfiguracji, ale nie są dowodem zachowania runtime.

URI handlery działają w pojedynczym server tasku. `httpd_queue_work` przenosi pracę
do tego samego kontekstu; spike używa go do eventu po handshake i broadcastu po
odebraniu tekstu. Trwały WS nie blokuje konstrukcyjnie acceptu HTTP, lecz długi
handler, `httpd_req_recv` albo send do wolnego klienta może zająć server task do
timeoutu. Brak wbudowanej per-client WS message queue/policy porównywalnej z Async;
aplikacja musi zaprojektować bounded queue, drop/disconnect i ownership payloadu.

WS obsługuje typy text, binary, continuation, close, ping i pong. Przy
`handle_ws_control_frames=false` control frames obsługuje serwer. Receive najpierw
zwraca długość, a caller dostarcza ograniczony bufor; callback dostaje pojedynczy
frame, nie złożoną wiadomość. `final` odzwierciedla FIN, natomiast `fragmented` nie
jest ustawiane dla RX. TX nie fragmentuje automatycznie; caller ustawia
`fragmented`/`final` i continuation frames.

POST `/api/spike/body` sprawdza `content_len` przed `httpd_req_recv`, przyjmuje
1–256 B do stałego bufora i zwraca 400 dla pustego oraz 413 dla większego body.
Odbiór może być wykonywany porcjami do caller-owned storage i nie wymaga pełnej
dynamicznej kopii. Chunked request nie jest wspierany w tej wersji. Multipart nie ma
wbudowanego parsera wysokiego poziomu i wymaga przyszłego bounded parsera/application
policy. Runtime 200/400/413 nie był wykonany.

Headers można odczytać w początkowym handlerze handshake przed odpowiedzią, więc
przyszła walidacja auth/cookie jest możliwa; nagłówki są usuwane po wysłaniu response.
Auth nie został zaimplementowany. SDK zawiera osobny `esp_https_server`, ale TLS nie
był budowany ani oceniany jako kryterium F8.2.

Ten backend także wymaga w Phase 9 jawnego przejścia z server tasku do Application/
Domain. Mniejszy koszt statyczny nie usuwa runtime heap w implementacji serwera i
Wi-Fi; musi on zostać zmierzony na urządzeniu.

## Compile i rozmiary

PlatformIO raportuje rozmiar sekcji użytych przez linker; `firmware.bin` jest
rzeczywistym plikiem wynikowym. Delta jest względem wspólnego transport-free Wi-Fi/AP
baseline i obejmuje cały kod konkretnego spike. IDF zawiera dodatkowy bounded POST,
więc różnica między kandydatami nie jest czystą deltą samych backendów.

| Environment | Wynik | RAM | Flash linker | Flash % | `firmware.bin` | Delta RAM | Delta flash linker | Delta bin |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `web_spike_baseline` | PASS | 43 408 B | 725 721 B | 55.4% | 732 304 B | — | — | — |
| `web_spike_async` | PASS | 43 900 B | 801 541 B | 61.2% | 808 112 B | +492 B | +75 820 B | +75 808 B |
| `web_spike_idf` | PASS | 43 416 B | 759 525 B | 57.9% | 766 096 B | +8 B | +33 804 B | +33 792 B |

Async ma dwie przypięte zewnętrzne biblioteki; IDF używa komponentu już obecnego w
Arduino-ESP32/ESP-IDF. Static RAM z linkera nie obejmuje runtime task stacks, kolejek,
połączeń i buforów tworzonych po starcie.

| Candidate | Compile | Ten sam port HTTP+WS | Architektura concurrency | External deps | Wynik |
| --- | --- | --- | --- | --- | --- |
| ESP32Async 3.6.0 + AsyncTCP 3.3.2 | PASS | Tak: jeden `AsyncWebServer(80)` | AsyncTCP task/event queue, wielu klientów w API; runtime niepotwierdzony | 2 | COMPILE FEASIBLE / HIL REQUIRED |
| IDF 4.4.7 `esp_http_server` | PASS | Tak: jeden `httpd_handle_t`, port 80 | Jeden server task, do 7 open client sockets; runtime niepotwierdzony | 0 | COMPILE FEASIBLE / HIL REQUIRED |

## Pomiary runtime

Hardware nie był dostępny. Nie wolno wyprowadzać wartości heap ani niezawodności z
raportu linkera.

| Candidate | Firmware | Flash % | RAM | Heap start/po serverze | Heap +1 WS | Heap +2 WS | Concurrent GET | Reconnect/resync | Slow client |
| --- | ---: | ---: | ---: | --- | --- | --- | --- | --- | --- |
| ESP32Async | 808 112 B | 61.2% | 43 900 B | NOT MEASURED | NOT MEASURED | NOT MEASURED | NOT RUN | NOT RUN | NOT RUN |
| `esp_http_server` | 766 096 B | 57.9% | 43 416 B | NOT MEASURED | NOT MEASURED | NOT MEASURED | NOT RUN | NOT RUN | NOT RUN |

Network loss, reconnect, 404, Async body behavior i IDF 200/400/413 są **NOT RUN**.
Spike nie dotyka `NetworkService`, `ApplicationRuntime`, Health ani Safety. Transport
nie jest zależny od `NormalProcessing`, więc proof nie wprowadza przeszkody dla
przyszłego użycia w Maintenance.

## Dokładna procedura F8.3 HIL

Procedurę wykonać osobno dla `web_spike_async` i `web_spike_idf`, zapisując log
serial, wynik każdego żądania oraz odczyty heap. Nie porównywać buildów z różnymi
flagami lub boardem.

1. Zbudować i wgrać kandydat:
   `platformio run -d spikes/web_transport_f8_2 -e <environment> -t upload`.
2. Otworzyć monitor 115200 i zapisać `before_network`, `before_server` oraz
   `after_server`: free heap, minimum free heap i largest 8-bit block.
3. Połączyć host z AP `aquaone-f8-spike`; potwierdzić adres z serial (zwykle
   `192.168.4.1`).
4. Wykonać `curl.exe -i http://192.168.4.1/api/spike/snapshot`; oczekiwać 200 i
   dokładnego snapshotu. Nieznana trasa ma zwrócić 404.
5. Otworzyć klienta A: `websocat ws://192.168.4.1/ws/spike`; pozostawić połączenie
   otwarte, odebrać event connect i zapisać heap dla 1 WS.
6. Przy aktywnym A wykonać co najmniej 50 kolejnych GET snapshotu z drugiego procesu;
   każde musi zwrócić 200 i ten sam pełny stan bez rozłączenia A.
7. Otworzyć klienta B, odebrać event connect, zapisać heap dla 2 WS i ponownie wykonać
   50 GET podczas aktywnych A+B.
8. Wysłać krótką wiadomość tekstową z A; oba klienty mają odebrać broadcast
   `spike.changed`. Powtórzyć serię i zapisać send errors/disconnects.
9. Rozłączyć A, zapisać heap, połączyć A ponownie, odebrać event i wykonać GET pełnego
   snapshotu. Replay/history nie jest oczekiwany.
10. Dla IDF wysłać POST o długości 1–256 B, pusty POST i >256 B; oczekiwać odpowiednio
    200, 400, 413. Dla Async dodać dopiero izolowany bounded body fixture i sprawdzić
    hard reject przed application accumulation.
11. Uruchomić klienta WS, który po connect nie czyta; generować broadcasty z drugiego
    klienta. Zmierzyć wzrost heap/kolejki, moment drop/close/error oraz dostępność HTTP
    i drugiego WS. Nie przyjmować docelowej policy na podstawie jednego przebiegu.
12. Wyłączyć Wi-Fi hosta, włączyć ponownie, połączyć z AP, odtworzyć WS i pobrać pełny
    snapshot. Osobno zrestartować ESP32 i powtórzyć resync; transport failure nie może
    zmieniać Core `OperationalState`, Health ani Safety.
13. Po każdym disconnect zapisać free/min/largest heap, odczekać 60 s i powtórzyć
    sekwencję co najmniej 10 razy, aby wykryć stały ubytek.

## Wniosek

Oba backendy spełniają compile-time część MUST na Arduino-ESP32 2.0.17: jeden
listener/port, HTTP snapshot, WS endpoint, wiele połączeń w modelu API, możliwe
bounded body i przewidywalny task boundary. Żaden nie ma jeszcze dowodu persistent
WS + concurrent HTTP, praktycznego limitu klientów, runtime heap, reconnect ani
slow-client behavior.

WEB-103 pozostaje **DECISION REQUIRED**. Do F8.3 HIL przechodzą oba warianty.
`esp_http_server` powinien być mierzony pierwszy, ponieważ ten sam compile proof ma
mniejszy flash/RAM i zero zewnętrznych zależności; ESP32Async musi pozostać w
porównaniu, ponieważ ma jawne per-client queues, broadcast status i politykę pełnej
kolejki. To kolejność pomiaru wynikająca z evidence, a nie wybór zwycięzcy.

Final verdict: **CANDIDATES COMPILE — HIL REQUIRED**.

Native safety regression: **368/368 PASS** w 11 suites. Produkcyjny kod Web i
pozostały Core nie zostały zmienione.
