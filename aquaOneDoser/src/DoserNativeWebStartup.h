#pragma once

#include <stddef.h>

#include <AquaCore/Web/CoreWebProjectionPublisher.h>
#include <AquaCore/Web/HttpStreamingServerTransport.h>
#include <AquaCore/Web/NativeWebService.h>

#include "DoserNativeOtaRoute.h"
#include "DoserNativeWeb.h"

// All preparation stays in the Application context. A failed precondition
// leaves the single physical listener stopped; there is no legacy fallback.
bool startDoserNativeWeb(
    AquaCore::Web::NativeWebService& service,
    AquaCore::Web::HttpStreamingServerTransport& streamingTransport,
    AquaCore::Web::CoreWebProjectionPublisher& publisher,
    DoserOtaApplication& otaApplication,
    DoserOtaCapacitySnapshot& capacitySnapshot,
    DoserNativeWebRoutes& normalRoutes,
    DoserNativeOtaRoute& otaRoute,
    size_t startupCapacity);
