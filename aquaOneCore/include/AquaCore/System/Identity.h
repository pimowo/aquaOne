#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AquaCore/System/DeviceIdentity.h"

namespace AquaCore {
namespace Identity {

// Canonical IDN-101 identity coexists with legacy display/build identity.
// RuntimeIdentity is a separate per-boot value.
enum class ValidationError : uint8_t {
    None,
    NullInput,
    EmptyRequiredField,
    TooLong,
    InvalidFormat,
    InvalidDeviceId
};

enum class ValidationField : uint8_t {
    None,
    DeviceType,
    DeviceId,
    FirmwareVersion,
    CoreVersion,
    HardwareVariant
};

struct ValidationResult {
    ValidationResult() = delete;
    ValidationResult(ValidationError errorValue, ValidationField fieldValue)
        : error(errorValue), field(fieldValue) {
    }

    ValidationError error;
    ValidationField field;

    bool isValid() const {
        return error == ValidationError::None;
    }
};

// Device type grammar is [a-z][a-z0-9_-]{0,22}.
class DeviceTypeToken {
public:
    static constexpr size_t CAPACITY = AquaCore::DeviceIdentity::DEVICE_TYPE_CAPACITY;
    DeviceTypeToken();
    ValidationResult assign(const char* value);
    bool isValid() const;
    const char* value() const;
    bool equals(const DeviceTypeToken& other) const;
private:
    char value_[CAPACITY];
    bool valid_;
};

// Full factory/base MAC48, independent of an active network interface.
class DeviceId {
public:
    static constexpr size_t BYTE_COUNT = 6U;
    static constexpr size_t TEXT_LENGTH = 12U;
    DeviceId();
    ValidationResult assign(const uint8_t* bytes, size_t length);
    bool isValid() const;
    bool format(char* destination, size_t capacity) const;
    bool equals(const DeviceId& other) const;
private:
    uint8_t bytes_[BYTE_COUNT];
    bool valid_;
};

class DeviceIdSource {
public:
    virtual bool read(DeviceId& out) const = 0;
protected:
    ~DeviceIdSource() = default;
};

// All fields below are required. No normalization or truncation.
// A default value is invalid; a failed assign empties ALL fields and invalidates
// the value, including after a previous successful assign. Getters return owned,
// NUL-terminated storage. No dynamic allocation; ordinary copies own their data.
// A non-null source must point to readable memory through its first NUL or for
// Capacity bytes when no NUL occurs.
class DeviceIdentity {
public:
    static constexpr size_t DEVICE_TYPE_CAPACITY = DeviceTypeToken::CAPACITY;

    DeviceIdentity();
    ValidationResult assign(const char* deviceType, const DeviceId& deviceId);
    bool isValid() const;
    const char* deviceType() const;
    const DeviceId& deviceId() const;
    bool equals(const DeviceIdentity& other) const;

private:
    DeviceTypeToken deviceType_;
    DeviceId deviceId_;
    bool valid_;
};

// Only firmware/Core versions have CURRENT sources. No synthetic build/git ID,
// dirty flag or timestamp. Callers supply versions from existing build constants.
class BuildIdentity {
public:
    static constexpr size_t FIRMWARE_VERSION_CAPACITY =
        AquaCore::DeviceIdentity::FIRMWARE_VERSION_CAPACITY;
    // CURRENT Diagnostics VERSION_TEXT_CAPACITY, without importing Diagnostics.
    static constexpr size_t CORE_VERSION_CAPACITY = 16U;

    BuildIdentity();
    ValidationResult assign(const char* firmwareVersion, const char* coreVersion);
    bool isValid() const;
    const char* firmwareVersion() const;
    const char* coreVersion() const;

private:
    char firmwareVersion_[FIRMWARE_VERSION_CAPACITY];
    char coreVersion_[CORE_VERSION_CAPACITY];
    bool valid_;
};

// CURRENT hardwareVariant is the only existing common hardware datum.
// A separate platform/revision schema remains IDN-101; no MAC requirement.
class HardwareIdentity {
public:
    static constexpr size_t HARDWARE_VARIANT_CAPACITY =
        AquaCore::DeviceIdentity::HARDWARE_VARIANT_CAPACITY;

    HardwareIdentity();
    ValidationResult assign(const char* hardwareVariant);
    bool isValid() const;
    const char* hardwareVariant() const;

private:
    char hardwareVariant_[HARDWARE_VARIANT_CAPACITY];
    bool valid_;
};

} // namespace Identity
} // namespace AquaCore
