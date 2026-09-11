# yoPilot

yoPilot is an Arduino firmware project for an ESP32-C3 remote controller for
yoRadio. It controls up to nine configured yoRadio instances over WebSocket
without ESPHome or Home Assistant dependencies.

## Hardware and UI

- ESP32-C3 and ST7735 LCD, 128x128 pixels.
- Wi-Fi reconnect, WebSocket session protection, and `RADIO_OFFLINE` after a
  connection timeout.
- RSSI indicator, battery level, clock, media-text scrolling, volume screen,
  and radio selection.

## Configuration

The firmware contains no private user Wi-Fi, password, IP, or radio data.
On a device without a saved configuration, it starts in Config Mode. Config
Mode provides a secured SoftAP and ConfigPortal for entering settings; the
resulting runtime configuration is stored in NVS.

Build-time, non-private configuration is kept in:

- `include/HardwareConfig.h` - display, ADC, and button wiring.
- `include/DeveloperConfig.h` - developer settings, including `DEBUG_UART`.
- `include/ConfigDefaults.h` - validated firmware defaults and limits.

## Build

Use PlatformIO from this directory:

```text
platformio run
```

Set `DEBUG_UART` to `1` in `DeveloperConfig.h` for extended serial diagnostics.