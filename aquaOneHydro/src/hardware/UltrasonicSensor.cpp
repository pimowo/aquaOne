#include "UltrasonicSensor.h"

UltrasonicSensor::UltrasonicSensor(
    uint8_t trigPin,
    uint8_t echoPin
)
    : trigPin_(trigPin),
      echoPin_(echoPin)
{
}

void UltrasonicSensor::configure(
    float minDistanceCm,
    float maxDistanceCm,
    uint32_t timeoutUs
)
{
    minDistanceCm_ = minDistanceCm;
    maxDistanceCm_ = maxDistanceCm;
    timeoutUs_ = timeoutUs;
}

void UltrasonicSensor::begin()
{
    digitalWrite(trigPin_, LOW);

    pinMode(trigPin_, OUTPUT);
    pinMode(echoPin_, INPUT);

    distanceCm_ = 0.0f;
    valid_ = false;
}

bool UltrasonicSensor::measure()
{
    digitalWrite(trigPin_, LOW);
    delayMicroseconds(2);

    digitalWrite(trigPin_, HIGH);
    delayMicroseconds(10);

    digitalWrite(trigPin_, LOW);

    const uint32_t durationUs =
        pulseIn(
            echoPin_,
            HIGH,
            timeoutUs_
        );

    if (durationUs == 0)
    {
        valid_ = false;
        return false;
    }

    const float measuredDistance =
        static_cast<float>(durationUs)
        * 0.0343f
        / 2.0f;

    if (
        measuredDistance < minDistanceCm_ ||
        measuredDistance > maxDistanceCm_
    )
    {
        valid_ = false;
        return false;
    }

    distanceCm_ = measuredDistance;
    valid_ = true;

    return true;
}

bool UltrasonicSensor::isValid() const
{
    return valid_;
}

float UltrasonicSensor::distanceCm() const
{
    return distanceCm_;
}