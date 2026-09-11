#include "AquaCore/System/DeviceIdentity.h"

namespace AquaCore {
namespace {

void copyText(
    char* destination,
    size_t capacity,
    const char* source
) {
    if (capacity == 0U) {
        return;
    }

    size_t index = 0U;

    if (source != nullptr) {
        while (
            index + 1U < capacity &&
            source[index] != '\0'
        ) {
            destination[index] = source[index];
            ++index;
        }
    }

    destination[index] = '\0';
}

} // namespace

DeviceIdentity::DeviceIdentity()
    : deviceType {},
      deviceName {},
      firmwareVersion {},
      hardwareVariant {} {
}

DeviceIdentity::DeviceIdentity(
    const char* deviceTypeValue,
    const char* deviceNameValue,
    const char* firmwareVersionValue,
    const char* hardwareVariantValue
) : DeviceIdentity() {
    copyText(
        deviceType,
        DEVICE_TYPE_CAPACITY,
        deviceTypeValue
    );
    copyText(
        deviceName,
        DEVICE_NAME_CAPACITY,
        deviceNameValue
    );
    copyText(
        firmwareVersion,
        FIRMWARE_VERSION_CAPACITY,
        firmwareVersionValue
    );
    copyText(
        hardwareVariant,
        HARDWARE_VARIANT_CAPACITY,
        hardwareVariantValue
    );
}

} // namespace AquaCore