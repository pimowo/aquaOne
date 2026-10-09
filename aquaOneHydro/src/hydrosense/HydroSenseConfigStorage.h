#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <AquaCore/Config/PreferencesStorageBackend.h>
#include <AquaCore/Config/StorageRecord.h>
#include <AquaCore/Config/StorageService.h>

#include "hydrosense/HydroSenseConfig.h"
#include "web/HydroWebAuthorities.h"

class HydroSenseConfigStorage final
    : public HydroConfigPersistence
{
public:
    HydroSenseConfigStorage();

    bool begin();

    bool load(
        HydroSenseConfig& config
    );

    bool save(
        const HydroSenseConfig& config
    ) override;

    bool hasValidConfig() const;

    AquaCore::Config::StorageStatus
    status() const override;

    static bool validateConfig(
        const HydroSenseConfig& config
    );

private:
    static constexpr uint16_t
        SCHEMA_VERSION = 3;

    static bool validatePayload(
        const void* payload,
        size_t payloadSize
    );

    AquaCore::Config::
        PreferencesStorageBackend<Preferences>
            backend_;

    uint8_t workspace_[
        AquaCore::Config::StorageRecord::HEADER_SIZE +
        sizeof(HydroSenseConfig)
    ] {};

    AquaCore::Config::StorageService
        storage_;
};
