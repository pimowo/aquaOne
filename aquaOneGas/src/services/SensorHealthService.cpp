#include "services/SensorHealthService.h"

namespace gassense {

void SensorHealthService::update(const Measurements& m) {
    // HX711 jest podstawowym czujnikiem GasSense.
    criticalFault_ = (m.hx711 == SensorHealth::Fault);
}

}
