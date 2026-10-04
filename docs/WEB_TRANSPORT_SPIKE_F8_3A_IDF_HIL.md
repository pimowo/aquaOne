# F8.3A — `esp_http_server` HIL na ESP32-S3

Wynik: **IDF HIL PASS** dla izolowanego kandydata z F8.2. WEB-103 pozostaje
**DECISION REQUIRED**. F8.3B musi osobno zmierzyć kandydata Async przed wyborem
backendu. Ten raport dotyczy wyłącznie spike'a, a nie produkcyjnego Web Core.

## Układ testu

- Fizyczna płytka na COM14: ESP32-S3 QFN56 rev 0.2, 4 MB XMC flash, 2 MB PSRAM
  (identyfikacja podczas uploadu). Tylko `web_spike_idf` używa profilu S3,
  jawnego 4 MB flash i USB CDC; środowiska baseline i Async z F8.2 nie zostały
  zmienione. Build i upload IDF przeszły.
- ESP działa jako Wi-Fi STA, uzyskuje adres z DHCP i uruchamia jeden
  `esp_http_server` na porcie 80. W udanym przebiegu adres wynosił
  `192.168.1.137`. PC pozostał w domowej sieci; po HIL odczyt stanu WLAN
  potwierdził połączenie, a połączenie TCP/443 do Internetu działało.
- Dane Wi-Fi pochodziły z lokalnego, ignorowanego przez Git nagłówka
  `.pio/f8_3a_credentials.h`, używanego tylko do builda IDF. Nagłówek oraz
  lokalny build IDF usunięto po HIL. Do odtworzenia testu trzeba wygenerować
  lokalnie ten nagłówek z definicjami `F8_3A_WIFI_SSID` i
  `F8_3A_WIFI_PASSWORD`, zbudować i wgrać `web_spike_idf`, a następnie ustawić
  `F83A_HOST` na IP wypisane przez Serial i uruchomić
  `spikes/web_transport_f8_2/tools/f8_3a_hil.py`. Wartości nie należą do repo.
  Wgrany na płytkę obraz HIL nadal zawiera dane STA do czasu zastąpienia go.

Serial potwierdził stabilny boot, `wifi_connected=true`, IP z DHCP i
`server_started=true`. Nie zaobserwowano nieplanowanego watchdogu, panic ani
rebootu podczas scenariuszy. Reset przez DTR po ich zakończeniu był celowy:
służył wyłącznie do ponownego odczytu H0/H1.

## Wyniki scenariuszy

| Scenariusz | Wynik i obserwacja |
| --- | --- |
| HTTP only | PASS: snapshot 200, w tym 20 równoległych GET. |
| 1 persistent WS | PASS: handshake 101 i event powitalny. |
| HTTP + 1 WS | PASS: 20 GET snapshotu przy otwartym WS; późniejszy event z tego WS potwierdził jego użyteczność. |
| 2 persistent WS | PASS: dwa połączenia otrzymały eventy. |
| HTTP + 2 WS | PASS: 20 GET przy dwóch otwartych WS; po GET oba nadal odbierały ramki. |
| Send sync/async | PASS: nadawca A otrzymał odpowiedź z `httpd_ws_send_frame`; klient B otrzymał broadcast z `httpd_ws_send_frame_async` o tej samej revision. Status powrotu async API nie był rejestrowany. |
| Reconnect i full resync | PASS: po zmianie stanu i rozłączeniu A, nowy WS i pełny HTTP snapshot wskazały revision 37, zgodną z eventem B. To dowodzi zachowania fixture: HTTP snapshot jest źródłem pełnego stanu, WS powiadamia o zmianie. |
| Slow reader | PASS dla badanego obciążenia: trzeci WS nie czytał podczas wysłania 32 eventów przez A (8,91 s). A odbierał odpowiedzi, wykonano 5 GET, a później wolny klient i B odebrały po 32 eventy. Nie mierzono zajętości kolejki ani wyników async send; B nie był odczytywany w trakcie emisji. Backpressure policy pozostaje otwarta. |
| POST body | PASS: 0 B → 400; 1 i 256 B → 200; 257 i 1024 B → 413. Handler sprawdza `Content-Length` przed `httpd_req_recv` i odbiera maksymalnie 256 B do stałego bufora. |
| WS frame | PASS: mały tekst, mały binary i 256 B przyjęte; ramka tekstowa 257 B spowodowała zamknięcie połączenia. Limit 256 B jest polityką fixture, nie limitem biblioteki. |
| Ping/pong i close | PASS: własny klient raw WS wysłał ping i odebrał pong o tym samym payloadzie. Po korekcie asercji harnessa osobny test na tej samej płytce potwierdził odpowiedź close (opcode 8) na wysłaną ramkę close; klient nie obsługuje tych ramek automatycznie. |
| Network loss/recovery | PASS: endpoint wyłącznie HIL `/api/spike/hil/network-loss` zlecił `WiFi.disconnect(false, false)` na ESP, potem ponowne `WiFi.begin`. Stary WS został zerwany; STA odzyskało DHCP IP, HTTP wrócił, nowy WS działał i pełny snapshot był zgodny z eventem. Host nie rozłączał Wi-Fi. |
| Stability | PASS: 61 s rzeczywistego przebiegu dla celu 60 s, z dwoma WS, 39 GET, 7 parami eventów i 1 POST. Każda operacja sieciowa ma timeout, a cały harness limit 360 s. |

Kluczowy proof WEB-001 w tym kandydacie to jeden listener/port 80, dwa
persistent WS i równoległe HTTP GET. Test nie definiuje finalnego protokołu
Realtime, polityki kolejki ani limitów Phase 9.

## Heap i czas odpowiedzi

Poniższe liczby to `free / min free / largest 8-bit block` w bajtach. **H0 i H1
pochodzą z celowego resetu po udanym HIL**, a H4, H2, H3, H5 i H6 z udanego
przebiegu scenariuszy. W tym przebiegu H4 został zapisany po dziesiątym GET,
**przed** otwarciem pierwszego WS. Dlatego jego `min free` 259608 B jest większe
od późniejszego H2 243524 B; tabela nie jest jedną chronologiczną sekwencją
H0–H6 z jednego bootu.

| Marker | Free | Min free | Largest |
| --- | ---: | ---: | ---: |
| H0 — przed siecią, reset kontrolny | 340072 | 334924 | 303092 |
| H1 — po starcie HTTPD, reset kontrolny | 277212 | 277212 | 262132 |
| H4 — po 10 GET, udany HIL | 262412 | 259608 | 245748 |
| H2 — 1 WS, udany HIL | 270836 | 243524 | 253940 |
| H3 — 2 WS, udany HIL | 272420 | 243524 | 253940 |
| H5 — po zamknięciu jednego WS, udany HIL | 273196 | 243524 | 253940 |
| H6 — po zamknięciu wszystkich WS, udany HIL | 275384 | 243524 | 253940 |

Po zamknięciu klientów free heap wracał w okolice 274–275 KB. W mierzonym
61-sekundowym przebiegu nie zaobserwowano narastającego ubytku free heap;
ten czas nie wystarcza do stwierdzenia braku wycieków w dłuższej pracy.

Harness uruchamia licznik bezpośrednio przed `HTTPConnection.request()` i
zatrzymuje go po pełnym `response.read()`. Pomiar obejmuje zestawienie TCP,
żądanie i odpowiedź; nie obejmuje odstępów `sleep`, oczekiwania na WS event,
przygotowania scenariusza ani reconnect Wi-Fi. To orientacyjne obserwacje HIL,
nie benchmark docelowego Web API.

| Warunek GET | Min | Średnia | Max | Próby |
| --- | ---: | ---: | ---: | ---: |
| HTTP only | 302,32 ms | 346,25 ms | 539,36 ms | 25 |
| HTTP + 1 WS | 296,64 ms | 307,28 ms | 319,74 ms | 20 |
| HTTP + 2 WS | 295,37 ms | 306,81 ms | 315,75 ms | 20 |

## Granice wniosku

HIL używa jednego testowego urządzenia, jednej sieci LAN i krótkiego przebiegu
stability. Nie sprawdza długiego soaku, wielu cykli reconnect, trwałego
backpressure, TLS, auth, OTA, finalnego schema API, serializacji zdarzeń,
replay/history ani produkcyjnej policy Health/Safety. POST i WS mają limity
testowego fixture. `esp_http_server` wykonuje handlery w swoim server tasku;
Phase 9 wymaga serializowanego przejścia z transportu do Application boundary,
bez bezpośredniego wywołania Domain lub hardware z handlera.

F8.2 pozostaje historycznym compile comparison; jego 368/368 native tests
przeszło. F8.3A nie zmieniło produkcyjnego kodu, więc tej regresji nie
uruchamiano ponownie. Async wymaga osobnego F8.3B HIL, zanim WEB-103 będzie
można rozstrzygnąć.
