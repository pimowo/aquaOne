#pragma once

// =========================
// LumaSense Build Configuration
// =========================


// =========================
// HARDWARE TARGET
// =========================

// Dostępne platformy:
//
// 1 = AQma WiFi LED Control
// 2 = LOLIN32 Lite - platforma testowa

#define LUMASENSE_HW_AQMA          1
#define LUMASENSE_HW_LOLIN32_TEST  2

// Aktualnie używana platforma:
#define LUMASENSE_HARDWARE LUMASENSE_HW_LOLIN32_TEST


// =========================
// DEBUG
// =========================

// Dodatkowe logi developerskie przez Serial.
//
// 0 = wyłączone
// 1 = włączone
#define LUMASENSE_DEBUG 1


// =========================
// ACCELERATED TIME
// =========================

// Przyspieszony czas do testowania pełnej doby.
//
// 0 = normalna praca
// 1 = tryb testowy
#define LUMASENSE_ACCELERATED_TIME 0


// =========================
// DEVELOPMENT CHECKS
// =========================

// Dodatkowe kontrole developerskie.
//
// 0 = wyłączone
// 1 = włączone
#define LUMASENSE_DEV_CHECKS 1


// =========================
// SERIAL
// =========================

#define LUMASENSE_SERIAL_BAUD 115200


// =========================
// RTC I2C PINS
// =========================

#if LUMASENSE_HARDWARE == LUMASENSE_HW_AQMA

    // AQma WiFi LED Control
    #define LUMASENSE_RTC_SDA_PIN 21
    #define LUMASENSE_RTC_SCL_PIN 22

#elif LUMASENSE_HARDWARE == LUMASENSE_HW_LOLIN32_TEST

    // LOLIN32 Lite - platforma testowa
    #define LUMASENSE_RTC_SDA_PIN 32
    #define LUMASENSE_RTC_SCL_PIN 33

#else

    #error "Nieznana platforma LumaSense"

#endif