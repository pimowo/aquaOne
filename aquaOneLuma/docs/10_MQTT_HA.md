# LumaSense — kontrakt MQTT / Home Assistant

## 1. Stan implementacji

MQTT i integracja Home Assistant nie są jeszcze zaimplementowane. Są opcjonalną warstwą sterowania i telemetrii. Brak brokera, HA, Wi-Fi albo Internetu nie może wpływać na RTC, harmonogram, profile, transition ani wyjścia.

Home Assistant nie jest źródłem harmonogramu czasu rzeczywistego. Autonomiczny Core pozostaje jedynym właścicielem bieżącej logiki lampy.

## 2. Granica integracji

```text
Home Assistant
→ MQTT
→ przyszły MqttManager
→ publiczne API LumaCore / walidowana konfiguracja
→ RuntimeState jako potwierdzenie
```

MqttManager nie może używać GPIO, LEDC ani bezpośrednio ustawiać requested/actual. Polecenia trybów przechodzą przez Core i podlegają tej samej hierarchii oraz regułom odrzucania co WWW.

## 3. Minimalny zakres v1

Podstawowe polecenia:

- wybór aktywnego profilu 0–4;
- SERVICE ON/OFF;
- OFF ON/OFF;
- restart.

Podstawowa telemetria:

- online/offline;
- `mode` i `returnMode`;
- aktywny oraz serwisowy profil;
- DAY/NIGHT i indeks etapu;
- `timeValid`, status RTC i przyszły status NTP;
- requested i actual dla dostępnych kanałów;
- `transitionActive`;
- gotowość hardware;
- wersja firmware, uptime, IP i RSSI;
- opcjonalnie szacowana moc, jeśli dane konfiguracyjne są poprawne.

MANUAL, CHANNEL_TEST, PREVIEW, SIMULATION oraz pełna edycja profili pozostają poza podstawowym sterowaniem HA. Mogą być raportowane diagnostycznie.

## 4. Potwierdzanie poleceń

Publikowany stan zawsze pochodzi z Core. Odebranie komendy nie jest potwierdzeniem jej wykonania.

```text
cmd/service = ON
→ próba wejścia przez Core
→ odczyt RuntimeState
→ publikacja rzeczywistego state/mode
```

Jest to konieczne, ponieważ polecenie może zostać odrzucone przez priorytet trybu. Retained command nie może odtwarzać OFF, SERVICE ani żadnego override po restarcie. Po restarcie urządzenie startuje w NORMAL i publikuje świeży stan.

## 5. Dostępność i reconnect

LWT może publikować `offline`, a poprawne połączenie `online`. Status MQTT opisuje wyłącznie integrację sieciową.

Po reconnect MqttManager publikuje pełny aktualny stan z urządzenia. Nie odtwarza stanu z retained telemetry ani założeń brokera. Pętla reconnect musi być nieblokująca i nie może opóźniać Core.

## 6. Home Assistant Discovery

Discovery jest planowane. Wszystkie encje jednego urządzenia muszą używać stabilnego identyfikatora, na przykład fragmentu MAC, i wspólnego opisu device.

Planowane typy:

- `select` dla profilu;
- `switch` dla SERVICE i OFF;
- `button` dla restartu;
- sensory trybu, etapu, czasu, kanałów i diagnostyki.

Kanały są sensorami tylko do odczytu. HA nie dostarcza bezpośrednich suwaków PWM.

## 7. Topics

Proponowany, jeszcze niewiążący układ:

```text
lumasense/<device-id>/availability
lumasense/<device-id>/state/...
lumasense/<device-id>/cmd/...
```

Dokładne nazwy, payloady, QoS, retain, wersjonowanie protokołu i format błędów są TODO. Każda komenda musi być walidowana przed przekazaniem dalej, a dane konfiguracyjne muszą korzystać z transakcji Storage.

## 8. Niezależność Core

Awaria parsowania, publikacji, discovery lub brokera pozostawia bieżący tryb i konfigurację bez zmian. Kod MQTT nie może być wymagany do uruchomienia lampy ani do odczytu DS3231.