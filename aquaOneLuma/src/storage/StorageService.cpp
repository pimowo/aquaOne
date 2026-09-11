#include "StorageService.h"

#include <type_traits>

#include "ConfigValidator.h"

namespace LumaSense {
namespace {

constexpr char STORAGE_NAMESPACE[] = "lumasense";
constexpr char SLOT_A_KEY[] = "cfg_a";
constexpr char SLOT_B_KEY[] = "cfg_b";

} // namespace

static_assert(
    std::is_trivially_copyable<DeviceConfig>::value,
    "DeviceConfig must remain trivially copyable for binary storage"
);

static_assert(
    sizeof(DeviceConfig) <= UINT32_MAX,
    "DeviceConfig is too large for the record format"
);

StorageService::StorageService()
    : storage_(
        backend_,
        STORAGE_NAMESPACE,
        SLOT_A_KEY,
        SLOT_B_KEY
    ) {
}

bool StorageService::begin() {
    return storage_.begin(
        sizeof(DeviceConfig),
        DEVICE_CONFIG_SCHEMA_VERSION,
        &StorageService::validatePayload
    );
}

bool StorageService::load(DeviceConfig& config) {
    return storage_.load(&config);
}

bool StorageService::save(const DeviceConfig& config) {
    return storage_.save(&config);
}

bool StorageService::hasValidConfig() const {
    return storage_.hasValidPayload();
}

bool StorageService::validatePayload(
    const void* payload,
    size_t payloadSize
) {
    if (
        payload == nullptr ||
        payloadSize != sizeof(DeviceConfig)
    ) {
        return false;
    }

    const DeviceConfig& config =
        *static_cast<const DeviceConfig*>(payload);

    return
        config.schemaVersion ==
            DEVICE_CONFIG_SCHEMA_VERSION &&
        ConfigValidator::validate(config);
}

const AquaCore::Config::StorageService& StorageService::aquaService() const {
    return storage_;
}

} // namespace LumaSense
