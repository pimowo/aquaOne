#pragma once

#include <stddef.h>
#include <stdint.h>

namespace LumaSense {

// Liczba logicznych kanałów LED
constexpr uint8_t CHANNEL_COUNT = 8;

// Liczba profili
constexpr uint8_t PROFILE_COUNT = 5;

// Liczba etapów dnia
constexpr uint8_t DAY_STAGE_COUNT = 8;

// Standardowe przejście pomiędzy większymi stanami
constexpr uint32_t STANDARD_TRANSITION_MS = 60'000;

// Przejście DZIEŃ ↔ NOC
constexpr uint32_t NIGHT_TRANSITION_MS = 5'000;

// Timeout testu pojedynczego kanału
constexpr uint32_t CHANNEL_TEST_TIMEOUT_MS = 5UL * 60UL * 1000UL;

// Wygładzenie zmian podczas PREVIEW
constexpr uint32_t PREVIEW_SMOOTH_MS = 750;

// Domyślna długość wejścia do symulacji
constexpr uint32_t SIMULATION_ENTRY_MS = 7'500;

// Pojedyncza, nieblokujaca proba synchronizacji NTP.
constexpr uint32_t RTC_READ_INTERVAL_MS = 1'000;
constexpr uint32_t DEBUG_STATUS_INTERVAL_MS = 10'000;
constexpr uint32_t TIME_DIAGNOSTICS_INTERVAL_MS = 30'000;

constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10'000;

// NTP koryguje RTC najwyzej okresowo. Harmonogram sieciowy
// pozostaje odpowiedzialnoscia warstwy wyzszej.
constexpr uint32_t NTP_SYNC_INTERVAL_MS =
    24UL * 60UL * 60UL * 1000UL;

constexpr uint8_t NTP_MAX_SERVERS = 3;
constexpr size_t NTP_SERVER_NAME_CAPACITY = 64;

// Zakres logicznych wartości światła
constexpr float LEVEL_MIN_PERCENT = 0.0f;
constexpr float LEVEL_MAX_PERCENT = 100.0f;

// Minuta doby
constexpr uint16_t MINUTES_PER_DAY = 1440;

// Wstępna konfiguracja PWM dla AQma.
// Finalne wartości zostaną potwierdzone testami fizycznymi.
constexpr uint32_t PWM_FREQUENCY_HZ = 1000;
constexpr uint8_t PWM_RESOLUTION_BITS = 12;
constexpr uint16_t PWM_MAX_VALUE =
    (1U << PWM_RESOLUTION_BITS) - 1U;

// Stałe pozycje 8 etapów w fotoperiodzie
constexpr float DAY_STAGE_POSITION[DAY_STAGE_COUNT] = {
    0.00f, // SunriseStart
    0.07f, // Morning
    0.20f, // Forenoon
    0.41f, // Noon
    0.68f, // Afternoon
    0.84f, // Evening
    0.93f, // Twilight
    1.00f  // Sunset
};

} // namespace LumaSense