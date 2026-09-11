# GasSense

Szkielet firmware dla GasSense przygotowany pod VS Code / PlatformIO i integrację z AquaCore.

## Status

To jest **szkielet architektoniczny**, nie gotowy firmware produkcyjny.

Najpierw mają zostać spięte:
1. AquaCore,
2. warstwa sprzętowa,
3. sterowniki,
4. serwisy,
5. logika urządzenia,
6. WWW / MQTT.

Pełne założenia są w `docs/SPECIFICATION.md`.

## Start w VS Code

1. Otwórz folder projektu w VS Code.
2. Zainstaluj PlatformIO.
3. Otwórz `platformio.ini`.
4. Podepnij lokalnie AquaCore albo przypnij jego wersję w `lib_deps`.
5. Zbuduj projekt.
6. Dopiero po potwierdzeniu hardware uruchamiaj sterowniki HX711 / ADS1115 / DS18B20.

## Główna zasada

GasSense zawiera wyłącznie logikę charakterystyczną dla GasSense.

Wi-Fi, MQTT, RTC/NTP, OTA, storage, watchdog, diagnostyka systemowa, wspólne tryby i szablon WWW powinny pochodzić z AquaCore.
