#include "HydroSenseSettingsApi.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

using AquaCore::Web::ContentType;
using AquaCore::Web::HttpMethod;
using AquaCore::Web::WebRequest;
using AquaCore::Web::WebResponseWriter;

HydroSenseSettingsApi::HydroSenseSettingsApi(
    HydroSenseConfig& config,
    HydroSenseConfigStorage& storage
)
    : config_(config),
      storage_(storage)
{
}

const char* HydroSenseSettingsApi::route() const
{
    return "/api/settings";
}

HttpMethod HydroSenseSettingsApi::method() const
{
    return HttpMethod::Post;
}

void HydroSenseSettingsApi::handle(
    const WebRequest& request,
    WebResponseWriter& response
)
{
    if (
        request.body == nullptr ||
        request.bodyLength == 0
    )
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Brak danych formularza"
        );

        response.endResponse();
        return;
    }

    HydroSenseConfig updated =
        config_;


    // =========================================================
    // PŁYWAK
    // =========================================================

    updated.floatActiveLow =
        hasFormKey(
            request.body,
            "floatActiveLow"
        );

    updated.floatUsePullup =
        hasFormKey(
            request.body,
            "floatUsePullup"
        );

    if (
        !parseUint32(
            request.body,
            "floatDebounceMs",
            updated.floatDebounceMs
        )
    )
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Niepoprawny debounce plywaka"
        );

        response.endResponse();
        return;
    }


    // =========================================================
    // ULTRASONIC
    // =========================================================

    if (
        !parseFloat(
            request.body,
            "ultrasonicMinDistanceCm",
            updated.ultrasonicMinDistanceCm
        ) ||
        !parseFloat(
            request.body,
            "ultrasonicMaxDistanceCm",
            updated.ultrasonicMaxDistanceCm
        ) ||
        !parseUint32(
            request.body,
            "ultrasonicTimeoutUs",
            updated.ultrasonicTimeoutUs
        )
    )
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Niepoprawne ustawienia czujnika odleglosci"
        );

        response.endResponse();
        return;
    }


    // =========================================================
    // ZBIORNIK RO
    // =========================================================

    if (
        !parseFloat(
            request.body,
            "tankEmptyDistanceCm",
            updated.tankEmptyDistanceCm
        ) ||
        !parseFloat(
            request.body,
            "tankFullDistanceCm",
            updated.tankFullDistanceCm
        ) ||
        !parseUint32(
            request.body,
            "tankSampleIntervalMs",
            updated.tankSampleIntervalMs
        ) ||
        !parseUint8(
            request.body,
            "tankMaxFailedSeries",
            updated.tankMaxFailedSeries
        )
    )
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Niepoprawne ustawienia zbiornika RO"
        );

        response.endResponse();
        return;
    }


    // =========================================================
    // REZERWA RO
    // =========================================================

    if (
        !parseFloat(
            request.body,
            "reserveLowPercent",
            updated.reserveLowPercent
        ) ||
        !parseFloat(
            request.body,
            "reserveCriticalPercent",
            updated.reserveCriticalPercent
        ) ||
        !parseFloat(
            request.body,
            "reserveHysteresisPercent",
            updated.reserveHysteresisPercent
        )
    )
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Niepoprawne progi rezerwy RO"
        );

        response.endResponse();
        return;
    }


    // =========================================================
    // DOLEWKA
    // =========================================================

    if (
        !parseUint32(
            request.body,
            "topupStartDelayMs",
            updated.topupStartDelayMs
        ) ||
        !parseUint32(
            request.body,
            "topupMaxPumpRuntimeMs",
            updated.topupMaxPumpRuntimeMs
        )
    )
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Niepoprawne ustawienia dolewki"
        );

        response.endResponse();
        return;
    }


    // =========================================================
    // WI-FI
    // =========================================================

    updated.wifiStaEnabled =
        hasFormKey(
            request.body,
            "staEnabled"
        );

    updated.wifiAutoReconnect =
        hasFormKey(
            request.body,
            "autoReconnect"
        );

    updated.wifiApEnabled =
        hasFormKey(
            request.body,
            "apEnabled"
        );

    if (
        !parseFormValue(
            request.body,
            "ssid",
            updated.wifiSsid,
            sizeof(updated.wifiSsid)
        )
    )
    {
        updated.wifiSsid[0] = '\0';
    }

    if (
        !parseFormValue(
            request.body,
            "password",
            updated.wifiPassword,
            sizeof(updated.wifiPassword)
        )
    )
    {
        updated.wifiPassword[0] = '\0';
    }

    if (
        !parseFormValue(
            request.body,
            "hostname",
            updated.wifiHostname,
            sizeof(updated.wifiHostname)
        )
    )
    {
        updated.wifiHostname[0] = '\0';
    }

    if (
        !parseFormValue(
            request.body,
            "apSsid",
            updated.wifiApSsid,
            sizeof(updated.wifiApSsid)
        )
    )
    {
        updated.wifiApSsid[0] = '\0';
    }

    if (
        !parseFormValue(
            request.body,
            "apPassword",
            updated.wifiApPassword,
            sizeof(updated.wifiApPassword)
        )
    )
    {
        updated.wifiApPassword[0] = '\0';
    }

    if (
        !parseUint32(
            request.body,
            "reconnectMs",
            updated.wifiReconnectIntervalMs
        )
    )
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Niepoprawny czas reconnect"
        );

        response.endResponse();
        return;
    }


    // =========================================================
    // ZAPIS
    // =========================================================

    if (!storage_.save(updated))
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Konfiguracja odrzucona przez walidator"
        );

        response.endResponse();
        return;
    }

    config_ = updated;

    response.beginResponse(
        200,
        ContentType::PlainText
    );

    response.writeText("OK");
    response.endResponse();

    restartRequested_ = true;
}

bool HydroSenseSettingsApi::restartRequested() const
{
    return restartRequested_;
}

void HydroSenseSettingsApi::clearRestartRequest()
{
    restartRequested_ = false;
}

bool HydroSenseSettingsApi::parseFormValue(
    const char* body,
    const char* key,
    char* output,
    size_t outputSize
)
{
    if (
        body == nullptr ||
        key == nullptr ||
        output == nullptr ||
        outputSize == 0
    )
    {
        return false;
    }

    output[0] = '\0';

    const size_t keyLength =
        std::strlen(key);

    const char* cursor = body;

    while (*cursor != '\0')
    {
        const char* entryEnd =
            std::strchr(
                cursor,
                '&'
            );

        if (entryEnd == nullptr)
        {
            entryEnd =
                cursor +
                std::strlen(cursor);
        }

        const char* equals =
            static_cast<const char*>(
                std::memchr(
                    cursor,
                    '=',
                    static_cast<size_t>(
                        entryEnd - cursor
                    )
                )
            );

        if (
            equals != nullptr &&
            static_cast<size_t>(
                equals - cursor
            ) == keyLength &&
            std::strncmp(
                cursor,
                key,
                keyLength
            ) == 0
        )
        {
            const char* valueStart =
                equals + 1;

            const size_t valueLength =
                static_cast<size_t>(
                    entryEnd - valueStart
                );

            return decodeUrl(
                valueStart,
                valueLength,
                output,
                outputSize
            );
        }

        if (*entryEnd == '\0')
        {
            break;
        }

        cursor =
            entryEnd + 1;
    }

    return false;
}

bool HydroSenseSettingsApi::hasFormKey(
    const char* body,
    const char* key
)
{
    char value[8] {};

    return parseFormValue(
        body,
        key,
        value,
        sizeof(value)
    );
}

bool HydroSenseSettingsApi::parseUint32(
    const char* body,
    const char* key,
    uint32_t& value
)
{
    char text[24] {};

    if (
        !parseFormValue(
            body,
            key,
            text,
            sizeof(text)
        ) ||
        text[0] == '\0'
    )
    {
        return false;
    }

    errno = 0;

    char* end = nullptr;

    const unsigned long parsed =
        std::strtoul(
            text,
            &end,
            10
        );

    if (
        errno != 0 ||
        end == text ||
        *end != '\0'
    )
    {
        return false;
    }

    value =
        static_cast<uint32_t>(
            parsed
        );

    return true;
}

bool HydroSenseSettingsApi::parseUint8(
    const char* body,
    const char* key,
    uint8_t& value
)
{
    uint32_t parsed = 0;

    if (
        !parseUint32(
            body,
            key,
            parsed
        ) ||
        parsed > 255
    )
    {
        return false;
    }

    value =
        static_cast<uint8_t>(
            parsed
        );

    return true;
}

bool HydroSenseSettingsApi::parseFloat(
    const char* body,
    const char* key,
    float& value
)
{
    char text[32] {};

    if (
        !parseFormValue(
            body,
            key,
            text,
            sizeof(text)
        ) ||
        text[0] == '\0'
    )
    {
        return false;
    }

    errno = 0;

    char* end = nullptr;

    const float parsed =
        std::strtof(
            text,
            &end
        );

    if (
        errno != 0 ||
        end == text ||
        *end != '\0' ||
        !std::isfinite(parsed)
    )
    {
        return false;
    }

    value = parsed;

    return true;
}

bool HydroSenseSettingsApi::decodeUrl(
    const char* input,
    size_t length,
    char* output,
    size_t outputSize
)
{
    if (
        input == nullptr ||
        output == nullptr ||
        outputSize == 0
    )
    {
        return false;
    }

    size_t out = 0;

    for (
        size_t i = 0;
        i < length;
        ++i
    )
    {
        char c = input[i];

        if (c == '+')
        {
            c = ' ';
        }
        else if (
            c == '%' &&
            i + 2 < length
        )
        {
            const int high =
                hexValue(
                    input[i + 1]
                );

            const int low =
                hexValue(
                    input[i + 2]
                );

            if (
                high < 0 ||
                low < 0
            )
            {
                return false;
            }

            c = static_cast<char>(
                (high << 4) | low
            );

            i += 2;
        }

        if (
            out + 1 >=
            outputSize
        )
        {
            return false;
        }

        output[out++] = c;
    }

    output[out] = '\0';

    return true;
}

int HydroSenseSettingsApi::hexValue(
    char c
)
{
    if (
        c >= '0' &&
        c <= '9'
    )
    {
        return c - '0';
    }

    if (
        c >= 'a' &&
        c <= 'f'
    )
    {
        return c - 'a' + 10;
    }

    if (
        c >= 'A' &&
        c <= 'F'
    )
    {
        return c - 'A' + 10;
    }

    return -1;
}