#pragma once

#include <Arduino.h>

#include "hardware/UltrasonicSensor.h"

class WaterTank
{
public:
    explicit WaterTank(
        UltrasonicSensor& sensor
    );

    void configure(
        float emptyDistanceCm,
        float fullDistanceCm,
        uint32_t sampleIntervalMs,
        uint8_t maxFailedSeries
    );

    void begin();
    void update();

    bool isValid() const;

    bool hasNewReading() const;
    void clearNewReading();

    bool hasSensorFault() const;

    float distanceCm() const;
    float levelCm() const;
    float levelPercent() const;

    uint32_t lastValidReadingMs() const;

private:
    static constexpr uint8_t SAMPLE_COUNT = 5;
    static constexpr uint8_t MIN_VALID_SAMPLES = 3;

    void finishSeries();
    void processSamples();

    float median(
        float* values,
        uint8_t count
    ) const;

    UltrasonicSensor& sensor_;

    float emptyDistanceCm_ = 40.0f;
    float fullDistanceCm_ = 5.0f;

    uint32_t sampleIntervalMs_ = 80;
    uint8_t maxFailedSeries_ = 3;

    float samples_[SAMPLE_COUNT];

    uint8_t attempts_ = 0;
    uint8_t validSamples_ = 0;
    uint8_t failedSeries_ = 0;

    uint32_t lastSampleMs_ = 0;
    uint32_t lastValidReadingMs_ = 0;

    float distanceCm_ = 0.0f;
    float levelCm_ = 0.0f;
    float levelPercent_ = 0.0f;

    bool valid_ = false;
    bool newReading_ = false;
    bool sensorFault_ = false;
};