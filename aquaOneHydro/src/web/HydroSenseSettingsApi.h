#pragma once

#include <AquaCore/Web/WebApiProvider.h>

#include "hydrosense/HydroSenseConfig.h"
#include "hydrosense/HydroSenseConfigStorage.h"

class HydroSenseSettingsApi final
    : public AquaCore::Web::WebApiProvider
{
public:
    HydroSenseSettingsApi(
        HydroSenseConfig& config,
        HydroSenseConfigStorage& storage
    );

    const char* route() const override;

    AquaCore::Web::HttpMethod
    method() const override;

    void handle(
        const AquaCore::Web::WebRequest& request,
        AquaCore::Web::WebResponseWriter& response
    ) override;

    bool restartRequested() const;
    void clearRestartRequest();

private:
    static bool parseFormValue(
        const char* body,
        const char* key,
        char* output,
        size_t outputSize
    );

    static bool hasFormKey(
        const char* body,
        const char* key
    );

    static bool parseUint32(
        const char* body,
        const char* key,
        uint32_t& value
    );

    static bool parseUint8(
        const char* body,
        const char* key,
        uint8_t& value
    );

    static bool parseFloat(
        const char* body,
        const char* key,
        float& value
    );

    static bool decodeUrl(
        const char* input,
        size_t length,
        char* output,
        size_t outputSize
    );

    static int hexValue(char c);

    HydroSenseConfig& config_;
    HydroSenseConfigStorage& storage_;

    bool restartRequested_ = false;
};