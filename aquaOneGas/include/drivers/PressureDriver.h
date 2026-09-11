#pragma once
#include "../config/GasSenseConfig.h"
#include "../domain/Types.h"

namespace gassense {

class PressureDriver {
public:
    bool begin(const GasSenseConfig& config);
    void update();

    int16_t raw() const;
    float pressureBar() const;
    SensorHealth health() const;

private:
    int16_t raw_ = 0;
    float pressureBar_ = 0.0f;
    SensorHealth health_ = SensorHealth::Disabled;
};

} // namespace gassense
