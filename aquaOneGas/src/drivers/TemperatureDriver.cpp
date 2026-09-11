#include "drivers/TemperatureDriver.h"

namespace gassense {

bool TemperatureDriver::begin(const GasSenseConfig& config) {
    health_ = config.bottleTemperatureEnabled
        ? SensorHealth::Initializing
        : SensorHealth::Disabled;
    return true;
}

void TemperatureDriver::update() {
    // TODO: DS18B20 bez blokowania głównej pętli.
}

float TemperatureDriver::temperatureC() const { return temperatureC_; }
SensorHealth TemperatureDriver::health() const { return health_; }

}
