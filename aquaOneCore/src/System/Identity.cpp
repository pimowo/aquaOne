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

DeviceTypeToken::DeviceTypeToken() : value_ {}, valid_(false) {}

ValidationResult DeviceTypeToken::assign(const char* value) {
    char candidate[CAPACITY] {};
    ValidationResult result = copyRequired(candidate, value, ValidationField::DeviceType);
    if (result.isValid()) {
        for (size_t i = 0U; candidate[i] != '\0'; ++i) {
            const unsigned char ch = static_cast<unsigned char>(candidate[i]);
            const bool legal = (ch >= 'a' && ch <= 'z') ||
                (i > 0U && ((ch >= '0' && ch <= '9') || ch == '_' || ch == '-'));
            if (!legal) {
                result = { ValidationError::InvalidFormat, ValidationField::DeviceType };
                break;
            }
        }
    }
    if (result.isValid()) memcpy(value_, candidate, sizeof(value_));
    else memset(value_, 0, sizeof(value_));
    valid_ = result.isValid();
    return result;
}

bool DeviceTypeToken::isValid() const { return valid_; }
const char* DeviceTypeToken::value() const { return value_; }
bool DeviceTypeToken::equals(const DeviceTypeToken& other) const {
    return valid_ && other.valid_ && strcmp(value_, other.value_) == 0;
}

DeviceId::DeviceId() : bytes_ {}, valid_(false) {}
ValidationResult DeviceId::assign(const uint8_t* bytes, size_t length) {
    ValidationResult result { ValidationError::None, ValidationField::None };
    if (bytes == nullptr) result = { ValidationError::NullInput, ValidationField::DeviceId };
    else if (length != BYTE_COUNT) result = { ValidationError::InvalidFormat, ValidationField::DeviceId };
    else {
        bool zero = true;
        bool broadcast = true;
        for (size_t i = 0U; i < BYTE_COUNT; ++i) {
            zero = zero && bytes[i] == 0U;
            broadcast = broadcast && bytes[i] == 0xFFU;
        }
        if (zero || broadcast || (bytes[0] & 1U) != 0U)
            result = { ValidationError::InvalidDeviceId, ValidationField::DeviceId };
    }
    if (result.isValid()) memcpy(bytes_, bytes, BYTE_COUNT);
    else memset(bytes_, 0, BYTE_COUNT);
    valid_ = result.isValid();
    return result;
}
bool DeviceId::isValid() const { return valid_; }
bool DeviceId::format(char* destination, size_t capacity) const {
    if (!valid_ || destination == nullptr || capacity < TEXT_LENGTH + 1U) return false;
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0U; i < BYTE_COUNT; ++i) {
        destination[2U * i] = digits[bytes_[i] >> 4U];
        destination[2U * i + 1U] = digits[bytes_[i] & 0x0FU];
    }
    destination[TEXT_LENGTH] = '\0';
    return true;
}
bool DeviceId::equals(const DeviceId& other) const {
    return valid_ && other.valid_ && memcmp(bytes_, other.bytes_, BYTE_COUNT) == 0;
}

DeviceIdentity::DeviceIdentity() : deviceType_(), deviceId_(), valid_(false) {}
ValidationResult DeviceIdentity::assign(const char* deviceType, const DeviceId& deviceId) {
    DeviceTypeToken candidate;
    ValidationResult result = candidate.assign(deviceType);
    if (result.isValid() && !deviceId.isValid())
        result = { ValidationError::InvalidDeviceId, ValidationField::DeviceId };
    deviceType_ = result.isValid() ? candidate : DeviceTypeToken {};
    deviceId_ = result.isValid() ? deviceId : DeviceId {};
    valid_ = result.isValid();
    return result;
}
bool DeviceIdentity::isValid() const { return valid_; }
const char* DeviceIdentity::deviceType() const { return deviceType_.value(); }
const DeviceId& DeviceIdentity::deviceId() const { return deviceId_; }
bool DeviceIdentity::equals(const DeviceIdentity& other) const {
    return valid_ && other.valid_ && deviceType_.equals(other.deviceType_) &&
        deviceId_.equals(other.deviceId_);
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
