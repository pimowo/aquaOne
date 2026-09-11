#include "drivers/WeightDriver.h"

namespace gassense {

bool WeightDriver::begin(const GasSenseConfig&) {
    // TODO: HX711 + konfiguracja pinów z HardwareConfig.
    // TODO: zastosować zapisany offset/scale.
    // TODO: wdrożyć filtr medianowy + EMA + detekcję stabilności.
    health_ = SensorHealth::Initializing;
    return true;
}

void WeightDriver::update() {
    // TODO: non-blocking sampling.
}

bool WeightDriver::tare() {
    // TODO: ręczna tara wyłącznie z WWW po potwierdzeniu.
    return false;
}

bool WeightDriver::calibrate(float) {
    // TODO: kalibracja znanym ciężarem.
    return false;
}

long WeightDriver::raw() const { return raw_; }
float WeightDriver::filteredKg() const { return filteredKg_; }
MeasurementStability WeightDriver::stability() const { return stability_; }
SensorHealth WeightDriver::health() const { return health_; }

}
