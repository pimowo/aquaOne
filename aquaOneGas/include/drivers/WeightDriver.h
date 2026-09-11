#pragma once
#include "../config/GasSenseConfig.h"
#include "../domain/Types.h"

namespace gassense {

class WeightDriver {
public:
    bool begin(const GasSenseConfig& config);
    void update();
    bool tare();
    bool calibrate(float knownMassKg);

    long raw() const;
    float filteredKg() const;
    MeasurementStability stability() const;
    SensorHealth health() const;

private:
    long raw_ = 0;
    float filteredKg_ = 0.0f;
    MeasurementStability stability_ = MeasurementStability::Unknown;
    SensorHealth health_ = SensorHealth::Initializing;
};

} // namespace gassense
