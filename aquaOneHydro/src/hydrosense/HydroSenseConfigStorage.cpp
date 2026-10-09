#include "HydroSenseConfigStorage.h"
#include "HydroSenseConfigValidator.h"

HydroSenseConfigStorage::
HydroSenseConfigStorage()
    : storage_(
          backend_,
          "hydrosense",
          "config_a",
          "config_b",
          {workspace_, sizeof(workspace_)}
      )
{
}

bool HydroSenseConfigStorage::begin()
{
    return storage_.begin(
        sizeof(HydroSenseConfig),
        SCHEMA_VERSION,
        validatePayload
    );
}

bool HydroSenseConfigStorage::load(
    HydroSenseConfig& config
)
{
    HydroSenseConfig loadedConfig {};

    if (!storage_.load(&loadedConfig))
    {
        return false;
    }

    config = loadedConfig;

    return true;
}

bool HydroSenseConfigStorage::save(
    const HydroSenseConfig& config
)
{
    return storage_.save(&config);
}

bool HydroSenseConfigStorage::
hasValidConfig() const
{
    return storage_.hasValidPayload();
}

AquaCore::Config::StorageStatus
HydroSenseConfigStorage::status() const
{
    return storage_.status();
}

bool HydroSenseConfigStorage::
validatePayload(
    const void* payload,
    size_t payloadSize
)
{
    if (payload == nullptr)
    {
        return false;
    }

    if (
        payloadSize !=
        sizeof(HydroSenseConfig)
    )
    {
        return false;
    }

    const auto* config =
        static_cast<
            const HydroSenseConfig*
        >(payload);

    return validateConfig(*config);
}

bool HydroSenseConfigStorage::
validateConfig(
    const HydroSenseConfig& config
)
{
    return validateHydroSenseConfig(config);
}
