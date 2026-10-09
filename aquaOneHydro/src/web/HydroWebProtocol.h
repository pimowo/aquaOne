#pragma once

#include <stddef.h>

#include "web/HydroWebTypes.h"

bool parseHydroControlRequest(
    const char* body,
    size_t bodyLength,
    HydroWebRequest& request
);

bool parseHydroSettingsRequest(
    const char* body,
    size_t bodyLength,
    HydroSettingsRequest& request
);
