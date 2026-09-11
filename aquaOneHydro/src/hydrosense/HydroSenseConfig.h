#pragma once

#include <Arduino.h>

struct HydroSenseConfig
{
    // =========================================================
    // PŁYWAK W AKWARIUM
    // =========================================================

    bool floatActiveLow = true;
    bool floatUsePullup = true;

    uint32_t floatDebounceMs = 100;


    // =========================================================
    // JSN-SR04T - ZBIORNIK RO
    // =========================================================

    float ultrasonicMinDistanceCm = 2.0f;
    float ultrasonicMaxDistanceCm = 450.0f;

    uint32_t ultrasonicTimeoutUs = 30000;


    // =========================================================
    // GEOMETRIA I POMIARY ZBIORNIKA RO
    // =========================================================

    float tankEmptyDistanceCm = 40.0f;
    float tankFullDistanceCm = 5.0f;

    uint32_t tankSampleIntervalMs = 80;

    uint8_t tankMaxFailedSeries = 3;


    // =========================================================
    // REZERWA WODY RO
    // =========================================================

    float reserveLowPercent = 25.0f;
    float reserveCriticalPercent = 10.0f;

    float reserveHysteresisPercent = 3.0f;


    // =========================================================
    // AUTOMATYCZNA DOLEWKA
    // =========================================================

    uint32_t topupStartDelayMs = 3000;

    uint32_t topupMaxPumpRuntimeMs = 120000;


    // =========================================================
    // WI-FI STA
    // =========================================================

    bool wifiStaEnabled = false;

    char wifiSsid[33] = "";
    char wifiPassword[65] = "";

    char wifiHostname[33] =
        "hydrosense";

    bool wifiAutoReconnect = true;

    uint32_t wifiReconnectIntervalMs =
        10000;


    // =========================================================
    // ACCESS POINT
    // =========================================================

    bool wifiApEnabled = true;

    char wifiApSsid[33] =
        "HydroSense-Setup";

    /*
     * Puste hasło = otwarty AP.
     *
     * Później konfiguracja WWW pozwoli
     * ustawić własne hasło.
     */
    char wifiApPassword[65] = "";
};