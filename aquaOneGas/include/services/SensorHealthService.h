#pragma once
#include "../domain/Types.h"

namespace gassense {

class SensorHealthService {
public:
    void update(const Measurements& measurements);
    bool hasCriticalSensorFault() const { return criticalFault_; }

private:
    bool criticalFault_ = false;
};

} // namespace gassense
