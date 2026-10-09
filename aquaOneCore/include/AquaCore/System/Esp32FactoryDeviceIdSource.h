#pragma once

#include "AquaCore/System/Identity.h"

namespace AquaCore {
namespace Identity {

class Esp32FactoryDeviceIdSource final : public DeviceIdSource {
public:
    bool read(DeviceId& out) const override;
};

} // namespace Identity
} // namespace AquaCore
