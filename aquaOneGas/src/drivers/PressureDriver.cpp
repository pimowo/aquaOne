#include "drivers/PressureDriver.h"

namespace gassense {

bool PressureDriver::begin(const GasSenseConfig& config) {
    if (!config.pressureEnabled) {
        health_ = SensorHealth::Disabled;
        return true;
    }

    // TODO: ADS1115 + kalibracja + filtr medianowy + EMA.
    health_ = SensorHealth::Initializing;
    return true;
}

void PressureDriver::update() {
    // TODO: non-blocking sampling + recovery.
}

int16_t PressureDriver::raw() const { return raw_; }
float PressureDriver::pressureBar() const { return pressureBar_; }
SensorHealth PressureDriver::health() const { return health_; }

}
