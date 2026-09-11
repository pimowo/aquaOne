#pragma once

#include <Arduino.h>

class UltrasonicSensor
{
public:
    UltrasonicSensor(
        uint8_t trigPin,
        uint8_t echoPin
    );

    void configure(
        float minDistanceCm,
        float maxDistanceCm,
        uint32_t timeoutUs
    );

    void begin();

    bool measure();

    bool isValid() const;
    float distanceCm() const;

private:
    uint8_t trigPin_;
    uint8_t echoPin_;

    float minDistanceCm_ = 2.0f;
    float maxDistanceCm_ = 450.0f;
    uint32_t timeoutUs_ = 30000;

    float distanceCm_ = 0.0f;
    bool valid_ = false;
};