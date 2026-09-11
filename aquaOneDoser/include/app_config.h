#pragma once

#define APP_NAME        "PMW AquaDoser"
#define APP_VERSION     "0.1.0"
#define HOSTNAME        "pmw-aquadoser"

// I2C - DS3231
#define PIN_I2C_SDA     8
#define PIN_I2C_SCL     9

// Strefa czasowa Polska
#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

// Limity logicznego bezpieczenstwa dozowania
constexpr float MAX_SINGLE_DOSE_ML = 100.0F;
constexpr float MAX_REMAINING_ML = 10000.0F;
constexpr float MAX_PUMP_RUNTIME_SEC = 60.0F;
constexpr uint32_t LOW_REMAINING_DOSES = 5;

constexpr uint8_t RTC_FAILURE_THRESHOLD = 3;
constexpr uint8_t RTC_RECOVERY_THRESHOLD = 3;
constexpr unsigned long RTC_READ_INTERVAL_MS = 1000UL;

// MQTT / Home Assistant Discovery
constexpr uint16_t MQTT_BUFFER_SIZE = 2048;
constexpr unsigned long MQTT_DISCOVERY_START_DELAY_MS = 1500UL;
constexpr uint8_t MQTT_DISCOVERY_MESSAGES_PER_LOOP = 1;
constexpr unsigned long MQTT_DISCOVERY_PUBLISH_INTERVAL_MS = 20UL;
constexpr unsigned long MQTT_DISCOVERY_RETRY_MS = 500UL;
constexpr unsigned long MQTT_DISCOVERY_RETRY_MAX_MS = 2000UL;

// Wi-Fi state machine
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 5000UL;
constexpr unsigned long WIFI_FAST_RETRY_MS = 1500UL;
