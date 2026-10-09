#include "HydroSenseDashboard.h"

#include <cstdio>

using AquaCore::Web::WebResponseWriter;

namespace
{

void writeIp(
    WebResponseWriter& response,
    const AquaCore::Network::IpAddress& ip
)
{
    char text[20];

    std::snprintf(
        text,
        sizeof(text),
        "%u.%u.%u.%u",
        static_cast<unsigned>(ip.octets[0]),
        static_cast<unsigned>(ip.octets[1]),
        static_cast<unsigned>(ip.octets[2]),
        static_cast<unsigned>(ip.octets[3])
    );

    response.writeText(text);
}

const char* networkStateName(
    AquaCore::Network::NetworkState state
)
{
    switch (state)
    {
        case AquaCore::Network::NetworkState::Disabled:
            return "DISABLED";

        case AquaCore::Network::NetworkState::Idle:
            return "IDLE";

        case AquaCore::Network::NetworkState::Connecting:
            return "CONNECTING";

        case AquaCore::Network::NetworkState::Connected:
            return "CONNECTED";

        case AquaCore::Network::NetworkState::Disconnected:
            return "DISCONNECTED";

        case AquaCore::Network::NetworkState::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}

}

HydroSenseDashboard::HydroSenseDashboard(
    const AquaCore::Web::PublishedSnapshot<SystemStatus>& status
)
    : status_(status)
{
}

const char* HydroSenseDashboard::route() const
{
    return "/";
}

const char* HydroSenseDashboard::title() const
{
    return "HydroSense";
}

void HydroSenseDashboard::render(
    WebResponseWriter& response
) const
{
    char number[32];
    SystemStatus status {};
    if (!status_.read(status))
    {
        response.writeText("<section class=\"card\"><h2>HydroSense</h2><p>Stan chwilowo niedostepny.</p></section>");
        return;
    }

    response.writeText(
        "<div class=\"grid\">"


        // =====================================================
        // AKWARIUM
        // =====================================================

        "<section class=\"card\">"
        "<h2>Akwarium</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Pływak</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.floatSensorActive
            ? "<span class=\"tag warn\">NISKI POZIOM</span>"
            : "<span class=\"tag ok\">OK</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Tryb</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.serviceMode
            ? "<span class=\"tag warn\">SERVICE</span>"
            : "<span class=\"tag ok\">NORMAL</span>"
    );

    response.writeText(
        "</span></div>"
        "</section>"


        // =====================================================
        // ZBIORNIK RO
        // =====================================================

        "<section class=\"card\">"
        "<h2>Zbiornik RO</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Pomiar</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.tankValid
            ? "<span class=\"tag ok\">OK</span>"
            : "<span class=\"tag err\">BRAK</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Poziom</span>"
        "<span class=\"value\"><strong>"
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
        "</strong></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Poziom wody</span>"
        "<span class=\"value\">"
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f cm",
        static_cast<double>(
            status.tankLevelCm
        )
    );

    response.writeText(number);

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Odległość czujnika</span>"
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
        "<span class=\"key\">Rezerwa</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        reserveStateName(
            status.reserveState
        )
    );

    response.writeText(
        "</span></div>"
        "</section>"


        // =====================================================
        // DOLEWKA
        // =====================================================

        "<section class=\"card\">"
        "<h2>Dolewka</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Stan automatu</span>"
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
            ? "<span class=\"tag warn\">WŁĄCZONA</span>"
            : "<span class=\"tag ok\">WYŁĄCZONA</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Zezwolenie pompy</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.pumpAllowed
            ? "<span class=\"tag ok\">TAK</span>"
            : "<span class=\"tag err\">NIE</span>"
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
        "</section>"


        // =====================================================
        // ALARM
        // =====================================================

        "<section class=\"card\">"
        "<h2>Alarm</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Stan</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.hasAlarm
            ? "<span class=\"tag err\">ALARM</span>"
            : "<span class=\"tag ok\">BRAK</span>"
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
        "<span class=\"key\">Buzzer</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.buzzerMuted
            ? "Wyciszony"
            : "Aktywny"
    );

    response.writeText(
        "</span></div>"
        "</section>"


        // =====================================================
        // SIEĆ
        // =====================================================

        "<section class=\"card\">"
        "<h2>Sieć</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Wi-Fi STA</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status.wifiConnected
            ? "<span class=\"tag ok\">POŁĄCZONO</span>"
            : "<span class=\"tag warn\">NIEPOŁĄCZONO</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Stan</span>"
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
        "<span class=\"key\">IP STA</span>"
        "<span class=\"value\">"
    );

    if (status.ipAddress.isSet())
    {
        writeIp(
            response,
            status.ipAddress
        );
    }
    else
    {
        response.writeText("-");
    }

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

    if (status.accessPointIpAddress.isSet())
    {
        writeIp(
            response,
            status.accessPointIpAddress
        );
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

        "</section>"

        "</div>"
    );
}

const char*
HydroSenseDashboard::topupStateName(
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
HydroSenseDashboard::reserveStateName(
    WaterReserve::State state
)
{
    switch (state)
    {
        case WaterReserve::State::Unknown:
            return "UNKNOWN";

        case WaterReserve::State::Ok:
            return "OK";

        case WaterReserve::State::Low:
            return "LOW";

        case WaterReserve::State::Critical:
            return "CRITICAL";
    }

    return "UNKNOWN";
}

const char*
HydroSenseDashboard::alarmCodeName(
    AlarmManager::Code code
)
{
    switch (code)
    {
        case AlarmManager::Code::None:
            return "BRAK";

        case AlarmManager::Code::TankSensorFault:
            return "BŁĄD CZUJNIKA RO";

        case AlarmManager::Code::TankLevelLow:
            return "NISKI POZIOM RO";

        case AlarmManager::Code::TankLevelCritical:
            return "KRYTYCZNY POZIOM RO";

        case AlarmManager::Code::PumpLockout:
            return "LOCKOUT POMPY";
    }

    return "NIEZNANY";
}

const char*
HydroSenseDashboard::severityName(
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
