#pragma once

#include <AquaCore/Config/StorageService.h>

#include "hydrosense/HydroSenseConfig.h"

class HydroConfigPersistence
{
public:
    virtual bool save(const HydroSenseConfig& config) = 0;
    virtual AquaCore::Config::StorageStatus status() const = 0;

protected:
    virtual ~HydroConfigPersistence() = default;
};

class HydroTopupActions
{
public:
    virtual bool isServiceMode() const = 0;
    virtual void setServiceMode(bool enabled) = 0;
    virtual void resetLockout() = 0;

protected:
    virtual ~HydroTopupActions() = default;
};

class HydroBuzzerActions
{
public:
    virtual void mute() = 0;

protected:
    virtual ~HydroBuzzerActions() = default;
};
