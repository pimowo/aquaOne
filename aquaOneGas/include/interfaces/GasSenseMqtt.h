#pragma once
#include "../domain/Types.h"

namespace gassense {

// Adapter do wspólnego mechanizmu MQTT / HA Discovery w AquaCore.
// Nie implementować tutaj reconnect, LWT ani brokera.
class GasSenseMqtt {
public:
    void registerEntities();
    void publishSnapshot(const Measurements& measurements, const GasState& state);
};

} // namespace gassense
