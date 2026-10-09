#include "HydroSenseDiagnosticsPage.h"

#include <cstdio>

#include <AquaCore/System/RestartReason.h>

using AquaCore::Web::WebResponseWriter;

HydroSenseDiagnosticsPage::
HydroSenseDiagnosticsPage(
    const AquaCore::Web::PublishedSnapshot<HydroDiagnosticsProjection>& projection
)
    : projection_(projection)
{
}

const char*
HydroSenseDiagnosticsPage::route() const
{
    return "/diagnostics";
}

const char*
HydroSenseDiagnosticsPage::title() const
{
    return "Diagnostyka";
}

void HydroSenseDiagnosticsPage::render(
    WebResponseWriter& response
) const
{
    char number[32];
    HydroDiagnosticsProjection projection {};
    if (!projection_.read(projection))
    {
        response.writeText("<section class=\"card\"><h2>Diagnostyka</h2><p>Dane chwilowo niedostepne.</p></section>");
        return;
    }
    const SystemStatus& status = projection.status;

    const AquaCore::Config::StorageStatus
        storageStatus =
            projection.storage;


    // =========================================================
    // SYSTEM
    // =========================================================

    response.writeText(
        "<div class=\"grid\">"

        "<section class=\"card\">"
        "<h2>System</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Stan</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        projection.system.ready
            ? "<span class=\"tag ok\">GOTOWY</span>"
            : "<span class=\"tag err\">BŁĄD</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Uptime</span>"
        "<span class=\"value\">"
    );

    writeUptime(
        response,
        projection.system.uptimeMs
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Restart</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        AquaCore::restartReasonName(
            projection.system.restartReason
        )
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Firmware</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        projection.system.identity.firmwareVersion
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">AquaCore</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        projection.system.aquaCoreVersion
    );

    response.writeText(
        "</span></div>"

        "</section>"


        // =====================================================
        // STORAGE
        // =====================================================

        "<section class=\"card\">"
        "<h2>Konfiguracja / Storage</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Backend</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        storageStatus.backendReady
            ? "<span class=\"tag ok\">OK</span>"
            : "<span class=\"tag err\">BŁĄD</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Poprawna konfiguracja</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        storageStatus.hasValidPayload
            ? "<span class=\"tag ok\">TAK</span>"
            : "<span class=\"tag err\">NIE</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Aktywny slot</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        storageSlotName(
            storageStatus.activeSlot
        )
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Generacja</span>"
        "<span class=\"value\">"
    );

    std::snprintf(
        number,
        sizeof(number),
        "%lu",
        static_cast<unsigned long>(
            storageStatus.activeGeneration
        )
    );

    response.writeText(number);

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Ostatni odczyt</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        storageResultName(
            storageStatus.lastLoadResult
        )
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Ostatni zapis</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        storageResultName(
            storageStatus.lastSaveResult
        )
    );

    response.writeText(
        "</span></div>"

        "</section>"


        // =====================================================
        // NETWORK
        // =====================================================

        "<section class=\"card\">"
        "<h2>Sieć</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Stan STA</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        networkStateName(
            status.networkState
        )
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">STA</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.wifiConnected
            ? "<span class=\"tag ok\">POŁĄCZONO</span>"
            : "<span class=\"tag warn\">BRAK</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">IP STA</span>"
        "<span class=\"value\">"
    );

    writeIp(
        response,
        status.ipAddress
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">RSSI</span>"
        "<span class=\"value\">"
    );

    if (status.wifiConnected)
    {
        std::snprintf(
            number,
            sizeof(number),
            "%ld dBm",
            static_cast<long>(
                status.wifiRssi
            )
        );

        response.writeText(number);
    }
    else
    {
        response.writeText("-");
    }

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Reconnect</span>"
        "<span class=\"value\">"
    );

    std::snprintf(
        number,
        sizeof(number),
        "%lu",
        static_cast<unsigned long>(
            status.networkReconnectCount
        )
    );

    response.writeText(number);

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Access Point</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.accessPointActive
            ? "<span class=\"tag ok\">AKTYWNY</span>"
            : "<span class=\"tag warn\">WYŁĄCZONY</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">IP AP</span>"
        "<span class=\"value\">"
    );

    writeIp(
        response,
        status.accessPointIpAddress
    );

    response.writeText(
        "</span></div>"

        "</section>"


        // =====================================================
        // CZUJNIKI
        // =====================================================

        "<section class=\"card\">"
        "<h2>Czujniki</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Pływak akwarium</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.floatSensorActive
            ? "AKTYWNY"
            : "NIEAKTYWNY"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Pomiar RO</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.tankValid
            ? "<span class=\"tag ok\">POPRAWNY</span>"
            : "<span class=\"tag warn\">BRAK</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Błąd czujnika RO</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.tankSensorFault
            ? "<span class=\"tag err\">TAK</span>"
            : "<span class=\"tag ok\">NIE</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Odległość</span>"
        "<span class=\"value\">"
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f cm",
        static_cast<double>(
            status.tankDistanceCm
        )
    );

    response.writeText(number);

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Poziom</span>"
        "<span class=\"value\">"
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f %%",
        static_cast<double>(
            status.tankLevelPercent
        )
    );

    response.writeText(number);

    response.writeText(
        "</span></div>"

        "</section>"


        // =====================================================
        // DOLEWKA
        // =====================================================

        "<section class=\"card\">"
        "<h2>Dolewka</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Automat</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        topupStateName(
            status.topupState
        )
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Pompa</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.pumpOn
            ? "<span class=\"tag warn\">ON</span>"
            : "<span class=\"tag ok\">OFF</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Zezwolenie</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.pumpAllowed
            ? "TAK"
            : "NIE"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">LOCKOUT</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.pumpLocked
            ? "<span class=\"tag err\">AKTYWNY</span>"
            : "<span class=\"tag ok\">BRAK</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">SERVICE</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.serviceMode
            ? "<span class=\"tag warn\">ON</span>"
            : "OFF"
    );

    response.writeText(
        "</span></div>"

        "</section>"


        // =====================================================
        // ALARM
        // =====================================================

        "<section class=\"card\">"
        "<h2>Alarmy</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Alarm aktywny</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.hasAlarm
            ? "<span class=\"tag err\">TAK</span>"
            : "<span class=\"tag ok\">NIE</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Kod</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        alarmCodeName(
            status.alarmCode
        )
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Poziom</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        severityName(
            status.alarmSeverity
        )
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Buzzer wyciszony</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.buzzerMuted
            ? "TAK"
            : "NIE"
    );

    response.writeText(
        "</span></div>"

        "</section>"

        "</div>"
    );
}

const char*
HydroSenseDiagnosticsPage::networkStateName(
    AquaCore::Network::NetworkState state
)
{
    using AquaCore::Network::NetworkState;

    switch (state)
    {
        case NetworkState::Disabled:
            return "DISABLED";

        case NetworkState::Idle:
            return "IDLE";

        case NetworkState::Connecting:
            return "CONNECTING";

        case NetworkState::Connected:
            return "CONNECTED";

        case NetworkState::Disconnected:
            return "DISCONNECTED";

        case NetworkState::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}

const char*
HydroSenseDiagnosticsPage::storageSlotName(
    AquaCore::Config::StorageSlot slot
)
{
    using AquaCore::Config::StorageSlot;

    switch (slot)
    {
        case StorageSlot::None:
            return "NONE";

        case StorageSlot::A:
            return "A";

        case StorageSlot::B:
            return "B";
    }

    return "UNKNOWN";
}

const char*
HydroSenseDiagnosticsPage::storageResultName(
    AquaCore::Config::StorageOperationResult result
)
{
    using AquaCore::Config::StorageOperationResult;

    switch (result)
    {
        case StorageOperationResult::NotAttempted:
            return "NOT_ATTEMPTED";

        case StorageOperationResult::Success:
            return "SUCCESS";

        case StorageOperationResult::Failure:
            return "FAILURE";

        case StorageOperationResult::NoChange:
            return "NO_CHANGE";

        case StorageOperationResult::InvalidArgument:
            return "INVALID_ARGUMENT";

        case StorageOperationResult::ValidationFailure:
            return "VALIDATION_FAILURE";

        case StorageOperationResult::BackendFailure:
            return "BACKEND_FAILURE";

        case StorageOperationResult::VerifyFailure:
            return "VERIFY_FAILURE";
    }

    return "UNKNOWN";
}

const char*
HydroSenseDiagnosticsPage::topupStateName(
    TopupController::State state
)
{
    switch (state)
    {
        case TopupController::State::Idle:
            return "IDLE";

        case TopupController::State::Waiting:
            return "WAITING";

        case TopupController::State::Pumping:
            return "PUMPING";

        case TopupController::State::Blocked:
            return "BLOCKED";

        case TopupController::State::Lockout:
            return "LOCKOUT";
    }

    return "UNKNOWN";
}

const char*
HydroSenseDiagnosticsPage::alarmCodeName(
    AlarmManager::Code code
)
{
    switch (code)
    {
        case AlarmManager::Code::None:
            return "NONE";

        case AlarmManager::Code::TankSensorFault:
            return "TANK_SENSOR_FAULT";

        case AlarmManager::Code::TankLevelLow:
            return "TANK_LEVEL_LOW";

        case AlarmManager::Code::TankLevelCritical:
            return "TANK_LEVEL_CRITICAL";

        case AlarmManager::Code::PumpLockout:
            return "PUMP_LOCKOUT";
    }

    return "UNKNOWN";
}

const char*
HydroSenseDiagnosticsPage::severityName(
    AlarmManager::Severity severity
)
{
    switch (severity)
    {
        case AlarmManager::Severity::None:
            return "NONE";

        case AlarmManager::Severity::Info:
            return "INFO";

        case AlarmManager::Severity::Warning:
            return "WARNING";

        case AlarmManager::Severity::Critical:
            return "CRITICAL";
    }

    return "UNKNOWN";
}

void HydroSenseDiagnosticsPage::writeIp(
    WebResponseWriter& response,
    const AquaCore::Network::IpAddress& ip
)
{
    if (!ip.isSet())
    {
        response.writeText("-");
        return;
    }

    char text[20];

    std::snprintf(
        text,
        sizeof(text),
        "%u.%u.%u.%u",
        static_cast<unsigned>(
            ip.octets[0]
        ),
        static_cast<unsigned>(
            ip.octets[1]
        ),
        static_cast<unsigned>(
            ip.octets[2]
        ),
        static_cast<unsigned>(
            ip.octets[3]
        )
    );

    response.writeText(text);
}

void HydroSenseDiagnosticsPage::writeUptime(
    WebResponseWriter& response,
    uint32_t uptimeMs
)
{
    const uint32_t totalSeconds =
        uptimeMs / 1000UL;

    const uint32_t days =
        totalSeconds / 86400UL;

    const uint32_t hours =
        (totalSeconds % 86400UL) /
        3600UL;

    const uint32_t minutes =
        (totalSeconds % 3600UL) /
        60UL;

    const uint32_t seconds =
        totalSeconds % 60UL;

    char text[48];

    std::snprintf(
        text,
        sizeof(text),
        "%lu d %02lu:%02lu:%02lu",
        static_cast<unsigned long>(days),
        static_cast<unsigned long>(hours),
        static_cast<unsigned long>(minutes),
        static_cast<unsigned long>(seconds)
    );

    response.writeText(text);
}
