# LumaSense — hardware AQma i LOLIN32 Lite

## 1. Wspólny kontrakt

Core widzi osiem logicznych kanałów przez `HardwareInterface`. Implementację wybiera `LUMASENSE_HARDWARE` w `include/BuildConfig.h`:

| Wartość | Klasa | RTC SDA/SCL |
|---|---|---|
| `LUMASENSE_HW_LOLIN32_TEST` | `Lolin32Hardware` | GPIO32 / GPIO33 |
| `LUMASENSE_HW_AQMA` | `AqmaHardware` | GPIO21 / GPIO22 |

Nie wolno zestawić pinów jednej platformy z klasą drugiej. Nieznana wartość kończy kompilację przez `#error`.

Obie klasy raportują osiem dostępnych kanałów. `isChannelAvailable()` mówi o fizycznej obecności kanału, a `isReady()` o powodzeniu całej inicjalizacji i braku wykrytego błędu wyjść.

## 2. Mapa PWM AQma

| Kanał | GPIO |
|---|---:|
| CH1 | 23 |
| CH2 | 19 |
| CH3 | 17 |
| CH4 | 16 |
| CH5 | 26 |
| CH6 | 25 |
| CH7 | 27 |
| CH8 | 13 |

Płytka używa ESP32-WROOM-32 na 30-pinowym DevKit. Sygnały PWM 3,3 V sterują zewnętrznymi driverami LED.

## 3. Mapa PWM LOLIN32 Lite

| Kanał | GPIO |
|---|---:|
| CH1 | 23 |
| CH2 | 19 |
| CH3 | 18 |
| CH4 | 17 |
| CH5 | 16 |
| CH6 | 25 |
| CH7 | 26 |
| CH8 | 27 |

GPIO32 i GPIO33 są na tej platformie zarezerwowane dla DS3231.

## 4. Konfiguracja PWM

Bieżące stałe są wspólne dla kanałów i platform:

- częstotliwość: 1000 Hz;
- rozdzielczość: 12 bitów;
- maksymalny duty: 4095;
- osobny kanał LEDC dla każdego CH1–CH8.

Są to wartości używane przez aktualny kod. Ewentualna zmiana wymaga testu z docelowym driverem LED.

## 5. Logiczne poziomy i inwersja

Hardware otrzymuje końcowy logiczny procent po LightEngine. Dopiero warstwa sprzętowa mapuje go na PWM i stosuje `pwmInverted`:

```text
logicalDuty = round(percent / 100 × 4095)
physicalDuty = pwmInverted ? 4095 - logicalDuty : logicalDuty
```

Kontrakt:

| Logiczny poziom | `pwmInverted=false` | `pwmInverted=true` |
|---|---:|---:|
| 0% (OFF) | duty 0 | duty 4095 |
| 100% | duty 4095 | duty 0 |

`pwmInverted` jest kopiowane z `ChannelConfig` podczas `hardware.begin()`. Jest ustawieniem sprzętowym; zmiana podczas pracy nie aktualizuje hardware i wymaga restartu oraz ponownego `begin()`.

## 6. Bezpieczny start

Dla każdego kanału `begin()`:

1. kopiuje polaryzację;
2. ustawia fizyczny poziom OFF w zatrzasku GPIO przed przełączeniem pinu na OUTPUT;
3. utrzymuje pad w OFF przez GPIO hold;
4. dołącza LEDC;
5. wpisuje do LEDC duty odpowiadające fizycznemu OFF;
6. zwalnia hold dopiero po poprawnym ustawieniu duty.

Normalne sterowanie wolno rozpocząć wyłącznie po `begin() == true` i `isReady() == true`.

## 7. Błędy wyjść

Błąd `ledcAttachChannel()`, `ledcWrite()`, GPIO hold lub detach powoduje niepowodzenie inicjalizacji albo utratę gotowości. Kod próbuje ustawić wszystkie kanały w fizycznym OFF. Po błędzie zapisu zatrzymuje LEDC na poziomie OFF i uznaje kanał za odłączony dopiero po potwierdzonym `ledcDetach()`.

Pętla główna przy braku gotowości wywołuje `allChannelsOff()` i nie wykonuje normalnych zapisów poziomów.

## 8. RTC i pozostałe zasoby AQma

DS3231 pracuje pod adresem I2C 0x68; SQW i 32K nie są używane. Pozostałe zasoby PCB, obecnie poza Core:

| GPIO | Funkcja |
|---:|---|
| 14 | buzzer |
| 15 | FAN przez ULN2803 |
| 2, 4, 5 | SSR/output przez ULN2803 |
| 32 | DS18B20 |
| 12 | JP1, przycisk do GND |

GPIO2, GPIO5, GPIO12 i GPIO15 są pinami wymagającymi ostrożności podczas bootu.

## 9. TODO sprzętowe

- potwierdzić fizycznie mapę i polaryzację wszystkich wyjść na docelowej płytce AQma;
- potwierdzić 1000 Hz / 12 bitów z docelowymi driverami pod kątem migotania, dźwięku i stabilności 1%;
- potwierdzić zachowanie samych pinów w czasie resetu, zanim firmware przejmie GPIO;
- ustalić pojemność flash konkretnego modułu i wyprowadzenia złącza SV2.