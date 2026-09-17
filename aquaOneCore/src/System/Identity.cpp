#include "AquaCore/System/Identity.h"

#include <string.h>

namespace AquaCore {
namespace Identity {
namespace {

template <size_t Capacity>
ValidationResult copyRequired(
    char (&destination)[Capacity],
    const char* source,
    ValidationField field
) {
    if (source == nullptr) {
        return { ValidationError::NullInput, field };
    }

    for (size_t length = 0U; length < Capacity; ++length) {
        if (source[length] == '\0') {
            if (length == 0U) {
                return { ValidationError::EmptyRequiredField, field };
            }
            memcpy(destination, source, length + 1U);
            return { ValidationError::None, ValidationField::None };
        }
    }

    return { ValidationError::TooLong, field };
}

} // namespace

DeviceIdentity::DeviceIdentity()
    : deviceType_ {}, valid_(false) {
}

ValidationResult DeviceIdentity::assign(const char* deviceType) {
    char candidate[DEVICE_TYPE_CAPACITY] {};
    const ValidationResult result = copyRequired(
        candidate, deviceType, ValidationField::DeviceType
    );
    // Stage before replacing storage so assign(deviceType()) is safe.
    memcpy(deviceType_, candidate, sizeof(deviceType_));
    valid_ = result.isValid();
    return result;
}

bool DeviceIdentity::isValid() const {
    return valid_;
}

const char* DeviceIdentity::deviceType() const {
    return deviceType_;
}

BuildIdentity::BuildIdentity()
    : firmwareVersion_ {}, coreVersion_ {}, valid_(false) {
}

ValidationResult BuildIdentity::assign(
    const char* firmwareVersion,
    const char* coreVersion
) {
    char firmwareCandidate[FIRMWARE_VERSION_CAPACITY] {};
    char coreCandidate[CORE_VERSION_CAPACITY] {};
    ValidationResult result = copyRequired(
        firmwareCandidate, firmwareVersion, ValidationField::FirmwareVersion
    );
    if (result.isValid()) {
        result = copyRequired(
            coreCandidate, coreVersion, ValidationField::CoreVersion
        );
    }

    if (result.isValid()) {
        memcpy(firmwareVersion_, firmwareCandidate, sizeof(firmwareVersion_));
        memcpy(coreVersion_, coreCandidate, sizeof(coreVersion_));
    } else {
        memset(firmwareVersion_, 0, sizeof(firmwareVersion_));
        memset(coreVersion_, 0, sizeof(coreVersion_));
    }
    valid_ = result.isValid();
    return result;
}

bool BuildIdentity::isValid() const {
    return valid_;
}

const char* BuildIdentity::firmwareVersion() const {
    return firmwareVersion_;
}

const char* BuildIdentity::coreVersion() const {
    return coreVersion_;
}

HardwareIdentity::HardwareIdentity()
    : hardwareVariant_ {}, valid_(false) {
}

ValidationResult HardwareIdentity::assign(const char* hardwareVariant) {
    char candidate[HARDWARE_VARIANT_CAPACITY] {};
    const ValidationResult result = copyRequired(
        candidate, hardwareVariant, ValidationField::HardwareVariant
    );
    memcpy(hardwareVariant_, candidate, sizeof(hardwareVariant_));
    valid_ = result.isValid();
    return result;
}

bool HardwareIdentity::isValid() const {
    return valid_;
}

const char* HardwareIdentity::hardwareVariant() const {
    return hardwareVariant_;
}

} // namespace Identity
} // namespace AquaCore
