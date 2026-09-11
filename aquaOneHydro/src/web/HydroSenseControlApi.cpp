#include "HydroSenseControlApi.h"

#include <cstring>

using AquaCore::Web::ContentType;
using AquaCore::Web::HttpMethod;
using AquaCore::Web::WebRequest;
using AquaCore::Web::WebResponseWriter;

HydroSenseControlApi::HydroSenseControlApi(
    TopupController& topupController,
    BuzzerController& buzzerController
)
    : topupController_(topupController),
      buzzerController_(buzzerController)
{
}

const char* HydroSenseControlApi::route() const
{
    return "/api/control";
}

HttpMethod HydroSenseControlApi::method() const
{
    return HttpMethod::Post;
}

void HydroSenseControlApi::handle(
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
            "Brak akcji"
        );

        response.endResponse();
        return;
    }

    char action[32] {};

    if (
        !readAction(
            request.body,
            action,
            sizeof(action)
        )
    )
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Niepoprawna akcja"
        );

        response.endResponse();
        return;
    }

    if (
        std::strcmp(
            action,
            "service_toggle"
        ) == 0
    )
    {
        topupController_.setServiceMode(
            !topupController_.isServiceMode()
        );
    }
    else if (
        std::strcmp(
            action,
            "mute"
        ) == 0
    )
    {
        buzzerController_.mute();
    }
    else if (
        std::strcmp(
            action,
            "reset_lockout"
        ) == 0
    )
    {
        /*
         * Reset LOCKOUT nie uruchamia pompy.
         *
         * Controller wróci do IDLE albo BLOCKED
         * zależnie od aktualnego zezwolenia.
         */
        topupController_.resetLockout();
    }
    else
    {
        response.beginResponse(
            400,
            ContentType::PlainText
        );

        response.writeText(
            "Nieznana akcja"
        );

        response.endResponse();
        return;
    }

    response.beginResponse(
        200,
        ContentType::PlainText
    );

    response.writeText("OK");
    response.endResponse();
}

bool HydroSenseControlApi::readAction(
    const char* body,
    char* output,
    size_t outputSize
)
{
    if (
        body == nullptr ||
        output == nullptr ||
        outputSize == 0
    )
    {
        return false;
    }

    static const char KEY[] =
        "action=";

    if (
        std::strncmp(
            body,
            KEY,
            sizeof(KEY) - 1
        ) != 0
    )
    {
        return false;
    }

    const char* value =
        body +
        sizeof(KEY) - 1;

    const char* end =
        std::strchr(
            value,
            '&'
        );

    if (end == nullptr)
    {
        end =
            value +
            std::strlen(value);
    }

    return decodeUrl(
        value,
        static_cast<size_t>(
            end - value
        ),
        output,
        outputSize
    );
}

bool HydroSenseControlApi::decodeUrl(
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

int HydroSenseControlApi::hexValue(
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