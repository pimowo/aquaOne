#pragma once

#include <stddef.h>

namespace AquaCore {

struct DeviceIdentity {
    static constexpr size_t DEVICE_TYPE_CAPACITY = 24U;
    static constexpr size_t DEVICE_NAME_CAPACITY = 32U;
    static constexpr size_t FIRMWARE_VERSION_CAPACITY = 24U;
    static constexpr size_t HARDWARE_VARIANT_CAPACITY = 32U;

    char deviceType[DEVICE_TYPE_CAPACITY];
    char deviceName[DEVICE_NAME_CAPACITY];
    char firmwareVersion[FIRMWARE_VERSION_CAPACITY];
    char hardwareVariant[HARDWARE_VARIANT_CAPACITY];

    DeviceIdentity();

    DeviceIdentity(
        const char* deviceTypeValue,
        const char* deviceNameValue,
        const char* firmwareVersionValue,
        const char* hardwareVariantValue
    );
};

} // namespace AquaCore