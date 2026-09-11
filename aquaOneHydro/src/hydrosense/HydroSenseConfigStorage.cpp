#include "HydroSenseConfigStorage.h"

HydroSenseConfigStorage::
HydroSenseConfigStorage()
    : storage_(
          backend_,
          "hydrosense",
          "config_a",
          "config_b"
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
    // =========================================================
    // PŁYWAK
    // =========================================================

    if (
        config.floatDebounceMs >
        5000
    )
    {
        return false;
    }


    // =========================================================
    // ULTRASONIC
    // =========================================================

    if (
        config.ultrasonicMinDistanceCm <
        0.5f
    )
    {
        return false;
    }

    if (
        config.ultrasonicMaxDistanceCm <=
        config.ultrasonicMinDistanceCm
    )
    {
        return false;
    }

    if (
        config.ultrasonicMaxDistanceCm >
        1000.0f
    )
    {
        return false;
    }

    if (
        config.ultrasonicTimeoutUs <
        1000
    )
    {
        return false;
    }

    if (
        config.ultrasonicTimeoutUs >
        100000
    )
    {
        return false;
    }


    // =========================================================
    // ZBIORNIK
    // =========================================================

    if (
        config.tankFullDistanceCm <
        0.0f
    )
    {
        return false;
    }

    if (
        config.tankEmptyDistanceCm <=
        config.tankFullDistanceCm
    )
    {
        return false;
    }

    if (
        config.tankSampleIntervalMs <
        20
    )
    {
        return false;
    }

    if (
        config.tankSampleIntervalMs >
        10000
    )
    {
        return false;
    }

    if (
        config.tankMaxFailedSeries == 0 ||
        config.tankMaxFailedSeries > 100
    )
    {
        return false;
    }


    // =========================================================
    // REZERWA RO
    // =========================================================

    if (
        config.reserveCriticalPercent <
            0.0f ||
        config.reserveCriticalPercent >
            100.0f
    )
    {
        return false;
    }

    if (
        config.reserveLowPercent <
            0.0f ||
        config.reserveLowPercent >
            100.0f
    )
    {
        return false;
    }

    if (
        config.reserveCriticalPercent >=
        config.reserveLowPercent
    )
    {
        return false;
    }

    if (
        config.reserveHysteresisPercent <
            0.0f ||
        config.reserveHysteresisPercent >
            20.0f
    )
    {
        return false;
    }


    // =========================================================
    // DOLEWKA
    // =========================================================

    if (
        config.topupStartDelayMs >
        600000
    )
    {
        return false;
    }

    if (
        config.topupMaxPumpRuntimeMs <
            1000 ||
        config.topupMaxPumpRuntimeMs >
            3600000
    )
    {
        return false;
    }


    // =========================================================
    // SIEĆ
    // =========================================================

    if (
        !isNullTerminated(
            config.wifiSsid,
            sizeof(config.wifiSsid)
        ) ||
        !isNullTerminated(
            config.wifiPassword,
            sizeof(config.wifiPassword)
        ) ||
        !isNullTerminated(
            config.wifiHostname,
            sizeof(config.wifiHostname)
        ) ||
        !isNullTerminated(
            config.wifiApSsid,
            sizeof(config.wifiApSsid)
        ) ||
        !isNullTerminated(
            config.wifiApPassword,
            sizeof(config.wifiApPassword)
        )
    )
    {
        return false;
    }

    if (
        config.wifiStaEnabled &&
        config.wifiSsid[0] == '\0'
    )
    {
        return false;
    }

    if (
        config.wifiAutoReconnect &&
        config.wifiStaEnabled &&
        config.wifiReconnectIntervalMs == 0
    )
    {
        return false;
    }

    if (
        config.wifiApEnabled &&
        config.wifiApSsid[0] == '\0'
    )
    {
        return false;
    }

    return true;
}

bool HydroSenseConfigStorage::
isNullTerminated(
    const char* text,
    size_t capacity
)
{
    if (
        text == nullptr ||
        capacity == 0
    )
    {
        return false;
    }

    for (
        size_t i = 0;
        i < capacity;
        ++i
    )
    {
        if (text[i] == '\0')
        {
            return true;
        }
    }

    return false;
}