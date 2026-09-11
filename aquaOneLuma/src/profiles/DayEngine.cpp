#include "DayEngine.h"

#include "Constants.h"

namespace LumaSense {

// =========================================================
// Smoothstep
//
// 0 -> 0
// 1 -> 1
//
// łagodny początek i koniec przejścia
// =========================================================

float DayEngine::smoothstep(
    float value
) const {
    if (value <= 0.0f) {
        return 0.0f;
    }

    if (value >= 1.0f) {
        return 1.0f;
    }

    return
        value *
        value *
        (
            3.0f -
            2.0f * value
        );
}

// =========================================================
// Wersja minutowa
//
// Zachowana tylko dla kompatybilności.
// =========================================================

DayCalculation DayEngine::calculate(
    const Profile& profile,
    uint16_t minuteOfDay
) const {
    const uint32_t secondOfDay =
        static_cast<uint32_t>(
            minuteOfDay % MINUTES_PER_DAY
        ) * 60UL;

    return calculateSeconds(
        profile,
        secondOfDay
    );
}

// =========================================================
// Wersja sekundowa
// =========================================================

DayCalculation DayEngine::calculateSeconds(
    const Profile& profile,
    uint32_t secondOfDay
) const {
    DayCalculation result {};

    // -----------------------------------------------------
    // Normalizacja do jednej doby
    // -----------------------------------------------------

    secondOfDay %= 86400UL;

    const uint32_t dayStartSecond =
        static_cast<uint32_t>(
            profile.dayStartMinute
        ) * 60UL;

    const uint32_t dayEndSecond =
        static_cast<uint32_t>(
            profile.dayEndMinute
        ) * 60UL;

    // -----------------------------------------------------
    // NOC
    //
    // Aktualnie zakładamy:
    // dayStart < dayEnd
    //
    // Czyli np.:
    // 08:00 -> 19:00
    // -----------------------------------------------------

    if (
        secondOfDay < dayStartSecond ||
        secondOfDay >= dayEndSecond
    ) {
        result.dayState =
            DayState::Night;

        result.currentStageIndex = 0;
        result.nextStageIndex = 0;
        result.segmentProgress = 0.0f;

        if (profile.nightEnabled) {
            result.levels =
                profile.nightLevels;
        } else {
            for (
                uint8_t ch = 0;
                ch < CHANNEL_COUNT;
                ++ch
            ) {
                result.levels.value[ch] =
                    0.0f;
            }
        }

        return result;
    }

    // -----------------------------------------------------
    // DZIEŃ
    // -----------------------------------------------------

    result.dayState =
        DayState::Day;

    const uint32_t photoperiodSeconds =
        dayEndSecond -
        dayStartSecond;

    const uint32_t elapsedSeconds =
        secondOfDay -
        dayStartSecond;

    // -----------------------------------------------------
    // Pozycje etapów w sekundach
    // -----------------------------------------------------

    uint32_t stageSecond[DAY_STAGE_COUNT] {};

    for (
        uint8_t i = 0;
        i < DAY_STAGE_COUNT;
        ++i
    ) {
        stageSecond[i] =
            dayStartSecond +
            static_cast<uint32_t>(
                static_cast<float>(
                    photoperiodSeconds
                ) *
                DAY_STAGE_POSITION[i]
            );
    }

    // -----------------------------------------------------
    // Szukamy aktualnego segmentu
    // -----------------------------------------------------

    uint8_t currentStage = 0;
    uint8_t nextStage = 1;

    for (
        uint8_t i = 0;
        i < DAY_STAGE_COUNT - 1;
        ++i
    ) {
        if (
            secondOfDay >= stageSecond[i] &&
            secondOfDay < stageSecond[i + 1]
        ) {
            currentStage = i;
            nextStage = i + 1;
            break;
        }
    }

    result.currentStageIndex =
        currentStage;

    result.nextStageIndex =
        nextStage;

    // -----------------------------------------------------
    // Postęp pomiędzy etapami
    // -----------------------------------------------------

    const uint32_t segmentStart =
        stageSecond[currentStage];

    const uint32_t segmentEnd =
        stageSecond[nextStage];

    const uint32_t segmentLength =
        segmentEnd -
        segmentStart;

    float progress = 0.0f;

    if (segmentLength > 0) {
        progress =
            static_cast<float>(
                secondOfDay -
                segmentStart
            ) /
            static_cast<float>(
                segmentLength
            );
    }

    if (progress < 0.0f) {
        progress = 0.0f;
    }

    if (progress > 1.0f) {
        progress = 1.0f;
    }

    result.segmentProgress =
        progress;

    const float curvedProgress =
        smoothstep(progress);

    // -----------------------------------------------------
    // Interpolacja wszystkich 8 kanałów
    // -----------------------------------------------------

    for (
        uint8_t ch = 0;
        ch < CHANNEL_COUNT;
        ++ch
    ) {
        const float from =
            profile
                .stages[currentStage]
                .levels
                .value[ch];

        const float to =
            profile
                .stages[nextStage]
                .levels
                .value[ch];

        result.levels.value[ch] =
            from +
            (
                to - from
            ) *
            curvedProgress;
    }

    return result;
}

} // namespace LumaSense