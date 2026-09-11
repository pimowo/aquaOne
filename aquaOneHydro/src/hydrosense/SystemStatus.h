#pragma once

#include <Arduino.h>

#include <AquaCore/Network/NetworkTypes.h>

#include "hydrosense/WaterReserve.h"
#include "hydrosense/TopupController.h"
#include "hydrosense/AlarmManager.h"

struct SystemStatus
{
    bool serviceMode = false;

    // =========================================================
    // AKWARIUM
    // =========================================================

    bool floatSensorActive = false;


    // =========================================================
    // POMPA
    // =========================================================

    bool pumpOn = false;
    bool pumpAllowed = false;
    bool pumpLocked = false;

    TopupController::State topupState =
        TopupController::State::Idle;


    // =========================================================
    // ZBIORNIK RO
    // =========================================================

    bool tankValid = false;
    bool tankSensorFault = false;

    float tankDistanceCm = 0.0f;
    float tankLevelCm = 0.0f;
    float tankLevelPercent = 0.0f;

    WaterReserve::State reserveState =
        WaterReserve::State::Unknown;


    // =========================================================
    // ALARM
    // =========================================================

    bool hasAlarm = false;

    AlarmManager::Code alarmCode =
        AlarmManager::Code::None;

    AlarmManager::Severity alarmSeverity =
        AlarmManager::Severity::None;

    bool buzzerMuted = false;


    // =========================================================
    // SIEĆ
    // =========================================================

    AquaCore::Network::NetworkState
        networkState =
            AquaCore::Network::
                NetworkState::Disabled;

    bool wifiConnected = false;

    int32_t wifiRssi = 0;

    AquaCore::Network::IpAddress
        ipAddress {};

    bool accessPointActive = false;

    AquaCore::Network::IpAddress
        accessPointIpAddress {};

    uint32_t networkReconnectCount = 0;
};