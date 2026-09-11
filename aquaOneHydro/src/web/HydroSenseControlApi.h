#pragma once

#include <AquaCore/Web/WebApiProvider.h>

#include "hydrosense/TopupController.h"
#include "hydrosense/BuzzerController.h"

class HydroSenseControlApi final
    : public AquaCore::Web::WebApiProvider
{
public:
    HydroSenseControlApi(
        TopupController& topupController,
        BuzzerController& buzzerController
    );

    const char* route() const override;

    AquaCore::Web::HttpMethod
    method() const override;

    void handle(
        const AquaCore::Web::WebRequest& request,
        AquaCore::Web::WebResponseWriter& response
    ) override;

private:
    static bool readAction(
        const char* body,
        char* output,
        size_t outputSize
    );

    static bool decodeUrl(
        const char* input,
        size_t length,
        char* output,
        size_t outputSize
    );

    static int hexValue(char c);

    TopupController& topupController_;
    BuzzerController& buzzerController_;
};