# LumaSense — produkcyjny boot flow

## 1. Kompozycja

`FirmwareApp` posiada `DeviceConfig` i `LumaCore` oraz korzysta z `HardwareInterface`, `StorageService` i `TimeService`. Produkcyjny `main.cpp` komponuje opcjonalne Network, Diagnostics i Web obok autonomicznego `FirmwareApp`.

## 2. Start

1. defaults;
2. `StorageService::begin()` i load;
3. wybór NVS/defaults oraz walidacja;
4. `HardwareInterface::begin(config.channels)`;
5. `TimeService::begin()`;
6. `LumaCore::begin(config, nowMs)`;
7. Serial;
8. `NetworkService::begin()`;
9. rejestracja stron/API;
10. `WebService::begin()`;
11. nieblokująca pętla.

Storage jest rozstrzygany przed PWM, aby pierwszy fizyczny OFF znał `pwmInverted`. Serial, Network i Web zaczynają się po Core. Nie ma startupowego `delay()`.

## 3. Storage, czas i restart

Poprawny rekord A/B daje config NVS. Pusty, uszkodzony lub niedostępny Storage daje zwalidowane defaults w RAM. Profil wybrany przez Web jest zapisany istniejącą transakcją przed aktywacją.

Nieważny czas nie zatrzymuje aplikacji, lecz utrzymuje wyjścia na 0. RTC jest odświeżane najwyżej raz na sekundę; odzyskanie czasu używa istniejącego przejścia 60 s.

Storage nie zapisuje trybów runtime. `LumaCore::begin()` zawsze resetuje je do NORMAL, ale odtwarza trwały `activeProfileIndex`.

## 4. Pętla

1. `FirmwareApp::update(nowMs)`;
2. `NetworkService::update(nowMs)`;
3. `LumaWebApp::update(nowMs)`;
4. `WebService::update()`;
5. raport adresu i opcjonalny log.

Światło zawsze ma pierwszeństwo. Network i Web nie czekają na Wi-Fi i ich błąd nie zmienia `FirmwareStatus::running`.

## 5. Fail-safe i diagnostyka

Błąd hardware zachowuje istniejący fail-safe OFF i zatrzymuje normalne PWM. Network/Web nie uczestniczą w tej ścieżce.

Log startu podaje wersje LumaSense/Aqua Core, wariant, restart reason, Hardware/RTC/Storage/Core/Time, źródło configu, mode oraz wynik Network, tras i Web. Po połączeniu podaje IP i URL.

## 6. Granice AC8

NTP, MQTT, Home Assistant, OTA, captive portal, edytor profili i uwierzytelnianie pozostają poza AC8. Lokalny HTTP jest przeznaczony dla zaufanej sieci LAN.
