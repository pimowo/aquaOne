#include "HydroSenseApi.h"

#include <cstdio>

#include <AquaCore/Web/WebTypes.h>

using AquaCore::Web::ContentType;
using AquaCore::Web::HttpMethod;
using AquaCore::Web::WebRequest;
using AquaCore::Web::WebResponseWriter;
using AquaCore::Web::writeJsonString;

namespace
{

bool writeBool(
    WebResponseWriter& response,
    bool value
)
{
    return response.writeText(
        value ? "true" : "false"
    );
}

bool writeFloat(
    WebResponseWriter& response,
    float value
)
{
    char text[32];

    const int length = std::snprintf(
        text,
        sizeof(text),
        "%.2f",
        static_cast<double>(value)
    );

    if (
        length <= 0 ||
        static_cast<size_t>(length) >=
            sizeof(text)
    )
    {
        return false;
    }

    return response.write(
        text,
        static_cast<size_t>(length)
    );
}

}

HydroSenseApi::HydroSenseApi(
    const SystemStatus& status
)
    : status_(status)
{
}

const char* HydroSenseApi::route() const
{
    return "/api/hydrosense";
}

HttpMethod HydroSenseApi::method() const
{
    return HttpMethod::Get;
}

void HydroSenseApi::handle(
    const WebRequest&,
    WebResponseWriter& response
)
{
    response.beginResponse(
        200,
        ContentType::Json
    );

    response.writeText("{");


    response.writeText("\"serviceMode\":");
    writeBool(
        response,
        status_.serviceMode
    );


    response.writeText(",\"floatSensorActive\":");
    writeBool(
        response,
        status_.floatSensorActive
    );


    response.writeText(",\"pump\":{");

    response.writeText("\"on\":");
    writeBool(
        response,
        status_.pumpOn
    );

    response.writeText(",\"allowed\":");
    writeBool(
        response,
        status_.pumpAllowed
    );

    response.writeText(",\"locked\":");
    writeBool(
        response,
        status_.pumpLocked
    );

    response.writeText(",\"state\":");

    writeJsonString(
        response,
        topupStateName(
            status_.topupState
        )
    );

    response.writeText("}");


    response.writeText(",\"tank\":{");

    response.writeText("\"valid\":");
    writeBool(
        response,
        status_.tankValid
    );

    response.writeText(",\"sensorFault\":");
    writeBool(
        response,
        status_.tankSensorFault
    );

    response.writeText(",\"distanceCm\":");
    writeFloat(
        response,
        status_.tankDistanceCm
    );

    response.writeText(",\"levelCm\":");
    writeFloat(
        response,
        status_.tankLevelCm
    );

    response.writeText(",\"levelPercent\":");
    writeFloat(
        response,
        status_.tankLevelPercent
    );

    response.writeText(",\"reserve\":");

    writeJsonString(
        response,
        reserveStateName(
            status_.reserveState
        )
    );

    response.writeText("}");


    response.writeText(",\"alarm\":{");

    response.writeText("\"active\":");
    writeBool(
        response,
        status_.hasAlarm
    );

    response.writeText(",\"code\":");

    writeJsonString(
        response,
        alarmCodeName(
            status_.alarmCode
        )
    );

    response.writeText(",\"severity\":");

    writeJsonString(
        response,
        severityName(
            status_.alarmSeverity
        )
    );

    response.writeText(",\"buzzerMuted\":");

    writeBool(
        response,
        status_.buzzerMuted
    );

    response.writeText("}");


    response.writeText("}");

    response.endResponse();
}

const char* HydroSenseApi::topupStateName(
    TopupController::State state
)
{
    switch (state)
    {
        case TopupController::State::Idle:
            return "idle";

        case TopupController::State::Waiting:
            return "waiting";

        case TopupController::State::Pumping:
            return "pumping";

        case TopupController::State::Blocked:
            return "blocked";

        case TopupController::State::Lockout:
            return "lockout";
    }

    return "unknown";
}

const char* HydroSenseApi::reserveStateName(
    WaterReserve::State state
)
{
    switch (state)
    {
        case WaterReserve::State::Unknown:
            return "unknown";

        case WaterReserve::State::Ok:
            return "ok";

        case WaterReserve::State::Low:
            return "low";

        case WaterReserve::State::Critical:
            return "critical";
    }

    return "unknown";
}

const char* HydroSenseApi::alarmCodeName(
    AlarmManager::Code code
)
{
    switch (code)
    {
        case AlarmManager::Code::None:
            return "none";

        case AlarmManager::Code::TankSensorFault:
            return "tank_sensor_fault";

        case AlarmManager::Code::TankLevelLow:
            return "tank_level_low";

        case AlarmManager::Code::TankLevelCritical:
            return "tank_level_critical";

        case AlarmManager::Code::PumpLockout:
            return "pump_lockout";
    }

    return "unknown";
}

const char* HydroSenseApi::severityName(
    AlarmManager::Severity severity
)
{
    switch (severity)
    {
        case AlarmManager::Severity::None:
            return "none";

        case AlarmManager::Severity::Info:
            return "info";

        case AlarmManager::Severity::Warning:
            return "warning";

        case AlarmManager::Severity::Critical:
            return "critical";
    }

    return "unknown";
}