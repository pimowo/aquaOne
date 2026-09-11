#pragma once
#include "../config/GasSenseConfig.h"
#include "../domain/Types.h"

namespace gassense {

class TemperatureDriver {
public:
    bool begin(const GasSenseConfig& config);
    void update();

    float temperatureC() const;
    SensorHealth health() const;

private:
    float temperatureC_ = 0.0f;
    SensorHealth health_ = SensorHealth::Disabled;
};

} // namespace gassense
