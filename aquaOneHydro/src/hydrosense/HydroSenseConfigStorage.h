#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <AquaCore/Config/PreferencesStorageBackend.h>
#include <AquaCore/Config/StorageService.h>

#include "hydrosense/HydroSenseConfig.h"

class HydroSenseConfigStorage
{
public:
    HydroSenseConfigStorage();

    bool begin();

    bool load(
        HydroSenseConfig& config
    );

    bool save(
        const HydroSenseConfig& config
    );

    bool hasValidConfig() const;

    AquaCore::Config::StorageStatus
    status() const;

private:
    static constexpr uint16_t
        SCHEMA_VERSION = 3;

    static bool validatePayload(
        const void* payload,
        size_t payloadSize
    );

    static bool validateConfig(
        const HydroSenseConfig& config
    );

    static bool isNullTerminated(
        const char* text,
        size_t capacity
    );

    AquaCore::Config::
        PreferencesStorageBackend<Preferences>
            backend_;

    AquaCore::Config::StorageService
        storage_;
};