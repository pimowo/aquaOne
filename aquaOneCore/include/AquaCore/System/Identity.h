#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AquaCore/System/DeviceIdentity.h"

namespace AquaCore {
namespace Identity {

// F1.3 coexistence with legacy AquaCore::DeviceIdentity; no consumer migration.
// These are the minimum fields backed by CURRENT data, not final IDN-101 fields.
// Capacities include the NUL byte and reuse CURRENT implementation limits.
// device_id, MAC/MAC6, final syntax/capacities and RuntimeIdentity remain open.
enum class ValidationError : uint8_t {
    None,
    NullInput,
    EmptyRequiredField,
    TooLong
};

enum class ValidationField : uint8_t {
    None,
    DeviceType,
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

// All fields below are required. Validation checks presence and byte length,
// not final identifier grammar or SemVer. No normalization or truncation.
// A default value is invalid; a failed assign empties ALL fields and invalidates
// the value, including after a previous successful assign. Getters return owned,
// NUL-terminated storage. No dynamic allocation; ordinary copies own their data.
// A non-null source must point to readable memory through its first NUL or for
// Capacity bytes when no NUL occurs.
class DeviceIdentity {
public:
    static constexpr size_t DEVICE_TYPE_CAPACITY =
        AquaCore::DeviceIdentity::DEVICE_TYPE_CAPACITY;

    DeviceIdentity();
    ValidationResult assign(const char* deviceType);
    bool isValid() const;
    const char* deviceType() const;

private:
    char deviceType_[DEVICE_TYPE_CAPACITY];
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
