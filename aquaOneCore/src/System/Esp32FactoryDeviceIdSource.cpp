#include "AquaCore/System/Esp32FactoryDeviceIdSource.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_mac.h>
#endif

namespace AquaCore {
namespace Identity {

bool Esp32FactoryDeviceIdSource::read(DeviceId& out) const {
#if defined(ARDUINO_ARCH_ESP32)
    uint8_t bytes[DeviceId::BYTE_COUNT] {};
    // Factory eFuse base MAC, independent of esp_base_mac_addr_set(), STA or AP.
    // IDN-101 rejects locally administered factory values at this backend
    // boundary; DeviceId remains a platform-neutral MAC48 value type.
    if (esp_efuse_mac_get_default(bytes) == ESP_OK &&
        (bytes[0] & 0x02U) == 0U)
        return out.assign(bytes, sizeof(bytes)).isValid();
#endif
    out = DeviceId {};
    return false;
}

} // namespace Identity
} // namespace AquaCore
