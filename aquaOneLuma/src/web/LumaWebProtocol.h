#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "AquaCore/Web/WebTypes.h"

#include "../app/FirmwareApp.h"

namespace LumaSense {
namespace Web {

enum class LumaWebRequestKind : uint8_t {
    SetMode,
    ExitManual,
    SetManual,
    SelectProfile
};

struct LumaWebRequest {
    LumaWebRequestKind kind = LumaWebRequestKind::SetMode;
    OperatingMode mode = OperatingMode::Normal;
    ChannelLevels levels {};
    uint16_t timeoutMinutes = 0U;
    uint8_t profileIndex = 0U;
};

static_assert(std::is_trivially_copyable<LumaWebRequest>::value,
              "Luma Web request must remain a copied value");

bool parseModeRequest(
    const char* body, size_t bodyLength,
    LumaWebRequest& request, const char*& error
);
bool parseProfileRequest(
    const char* body, size_t bodyLength,
    LumaWebRequest& request, const char*& error
);
bool parseManualRequest(
    const char* body, size_t bodyLength,
    LumaWebRequest& request, const char*& error
);

void writeLumaError(
    AquaCore::Web::WebResponseWriter& response,
    uint16_t status,
    const char* message
);
void writeFirmwareCommandResult(
    AquaCore::Web::WebResponseWriter& response,
    FirmwareCommandResult result
);

} // namespace Web
} // namespace LumaSense
