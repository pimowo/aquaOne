#pragma once

// Konfiguracja sprzętowa kompilowana dla płytki yoPILOT ESP32-C3.

// === WYŚWIETLACZ ============================================================
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 128
#define DISPLAY_ROTATION 0

#define DISPLAY_CS 2
#define DISPLAY_DC 0
#define DISPLAY_RST 5
#define DISPLAY_MOSI 4
#define DISPLAY_SCLK 3

#define DISPLAY_SPI_HZ 40000000UL
#define DISPLAY_SPI_MODE 0
#define DISPLAY_X_OFFSET 2
#define DISPLAY_Y_OFFSET 3

// === BATERIA ================================================================
#define BATTERY_ADC_PIN 1
#define BATTERY_MIN_MV 3000
#define BATTERY_MAX_MV 4200
#define BATTERY_DIVIDER_RATIO 2.0

// === PRZYCISKI / WEJŚCIE ====================================================
#define BUTTON_PLAY_PIN 9
#define BUTTON_UP_PIN 8
#define BUTTON_DOWN_PIN 10

// === ZASILANIE / PODŚWIETLENIE ==============================================
// GPIO7 wymaga sprzętowo ograniczonego sygnału 0–3,3 V; nigdy nie podłączaj USB 5 V bezpośrednio.
#define PIN_LCD_BACKLIGHT 6
#define PIN_USB_PRESENT 7
#define PIN_CHARGE_STATUS 20
#define USB_PRESENT_ACTIVE_LEVEL HIGH
#define CHARGE_STATUS_ACTIVE_LEVEL LOW
#define USB_PRESENT_USE_INTERNAL_PULLUP 0
#define CHARGE_STATUS_USE_INTERNAL_PULLUP 1
#define POWER_STATUS_POLL_MS 100UL
#define POWER_STATUS_DEBOUNCE_MS 200UL
#define LCD_BACKLIGHT_PWM_HZ 20000UL
#define LCD_BACKLIGHT_PWM_RESOLUTION_BITS 10