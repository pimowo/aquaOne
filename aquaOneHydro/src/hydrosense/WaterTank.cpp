#include "WaterTank.h"

WaterTank::WaterTank(
    UltrasonicSensor& sensor
)
    : sensor_(sensor)
{
}

void WaterTank::configure(
    float emptyDistanceCm,
    float fullDistanceCm,
    uint32_t sampleIntervalMs,
    uint8_t maxFailedSeries
)
{
    emptyDistanceCm_ = emptyDistanceCm;
    fullDistanceCm_ = fullDistanceCm;
    sampleIntervalMs_ = sampleIntervalMs;
    maxFailedSeries_ = maxFailedSeries;
}

void WaterTank::begin()
{
    attempts_ = 0;
    validSamples_ = 0;
    failedSeries_ = 0;

    lastSampleMs_ = millis();
    lastValidReadingMs_ = 0;

    distanceCm_ = 0.0f;
    levelCm_ = 0.0f;
    levelPercent_ = 0.0f;

    valid_ = false;
    newReading_ = false;
    sensorFault_ = false;
}

void WaterTank::update()
{
    const uint32_t now = millis();

    if (
        (now - lastSampleMs_) <
        sampleIntervalMs_
    )
    {
        return;
    }

    lastSampleMs_ = now;

    ++attempts_;

    if (sensor_.measure())
    {
        if (validSamples_ < SAMPLE_COUNT)
        {
            samples_[validSamples_++] =
                sensor_.distanceCm();
        }
    }

    if (attempts_ >= SAMPLE_COUNT)
    {
        finishSeries();
    }
}

void WaterTank::finishSeries()
{
    if (
        validSamples_ >=
        MIN_VALID_SAMPLES
    )
    {
        processSamples();

        if (valid_)
        {
            failedSeries_ = 0;
            sensorFault_ = false;
            lastValidReadingMs_ = millis();
        }
    }
    else
    {
        valid_ = false;
    }

    if (!valid_)
    {
        if (failedSeries_ < 255)
        {
            ++failedSeries_;
        }

        if (
            failedSeries_ >=
            maxFailedSeries_
        )
        {
            sensorFault_ = true;
        }
    }

    newReading_ = true;

    attempts_ = 0;
    validSamples_ = 0;
}

void WaterTank::processSamples()
{
    const float usableHeight =
        emptyDistanceCm_ -
        fullDistanceCm_;

    if (usableHeight <= 0.0f)
    {
        valid_ = false;
        return;
    }

    distanceCm_ =
        median(
            samples_,
            validSamples_
        );

    levelCm_ =
        emptyDistanceCm_ -
        distanceCm_;

    if (levelCm_ < 0.0f)
    {
        levelCm_ = 0.0f;
    }

    if (levelCm_ > usableHeight)
    {
        levelCm_ = usableHeight;
    }

    levelPercent_ =
        (levelCm_ / usableHeight)
        * 100.0f;

    valid_ = true;
}

bool WaterTank::isValid() const
{
    return valid_;
}

bool WaterTank::hasNewReading() const
{
    return newReading_;
}

void WaterTank::clearNewReading()
{
    newReading_ = false;
}

bool WaterTank::hasSensorFault() const
{
    return sensorFault_;
}

float WaterTank::distanceCm() const
{
    return distanceCm_;
}

float WaterTank::levelCm() const
{
    return levelCm_;
}

float WaterTank::levelPercent() const
{
    return levelPercent_;
}

uint32_t WaterTank::lastValidReadingMs() const
{
    return lastValidReadingMs_;
}

float WaterTank::median(
    float* values,
    uint8_t count
) const
{
    for (
        uint8_t i = 0;
        i < count - 1;
        ++i
    )
    {
        for (
            uint8_t j = i + 1;
            j < count;
            ++j
        )
        {
            if (values[j] < values[i])
            {
                const float temp =
                    values[i];

                values[i] =
                    values[j];

                values[j] =
                    temp;
            }
        }
    }

    if ((count % 2) == 1)
    {
        return values[count / 2];
    }

    const uint8_t upper =
        count / 2;

    const uint8_t lower =
        upper - 1;

    return (
        values[lower] +
        values[upper]
    ) / 2.0f;
}