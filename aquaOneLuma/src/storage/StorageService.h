#pragma once

#ifdef LUMASENSE_STORAGE_PREFERENCES_HEADER
#include LUMASENSE_STORAGE_PREFERENCES_HEADER
#else
#include <Preferences.h>
#endif

#include "AquaCore/Config/PreferencesStorageBackend.h"
#include "AquaCore/Config/StorageService.h"
#include "ConfigTypes.h"

namespace LumaSense {

class StorageService {
public:
    StorageService();

    StorageService(const StorageService&) = delete;
    StorageService& operator=(const StorageService&) = delete;

    bool begin();
    bool load(DeviceConfig& config);
    bool save(const DeviceConfig& config);
    bool hasValidConfig() const;
    const AquaCore::Config::StorageService& aquaService() const;

private:
    using PreferencesBackend =
        AquaCore::Config::PreferencesStorageBackend<Preferences>;

    PreferencesBackend backend_;
    AquaCore::Config::StorageService storage_;

    static bool validatePayload(
        const void* payload,
        size_t payloadSize
    );
};

} // namespace LumaSense
