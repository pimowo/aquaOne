#include "DoserNativeWebStartup.h"

#include <stdint.h>

#include <AquaCore/Web/WebConfig.h>

bool startDoserNativeWeb(
    AquaCore::Web::NativeWebService& service,
    AquaCore::Web::HttpStreamingServerTransport& streamingTransport,
    AquaCore::Web::CoreWebProjectionPublisher& publisher,
    DoserOtaApplication& otaApplication,
    DoserOtaCapacitySnapshot& capacitySnapshot,
    DoserNativeWebRoutes& normalRoutes,
    DoserNativeOtaRoute& otaRoute,
    size_t startupCapacity) {
    if (startupCapacity == 0U || startupCapacity > UINT32_MAX) return false;

    const AquaCore::Web::CoreWebPublicationResult published = publisher.update();
    if (!published.systemPublished || !published.diagnosticsPublished ||
        !otaApplication.publishCapacity()) return false;

    DoserOtaCapacity capacity {};
    if (!capacitySnapshot.read(capacity) || !capacity.available ||
        capacity.availableBytes != startupCapacity) return false;

    if (!normalRoutes.addTo(service) ||
        !otaRoute.addTo(streamingTransport, static_cast<uint32_t>(startupCapacity)))
        return false;

    AquaCore::Web::WebConfig config {};
    config.enabled = true;
    config.port = 80U;
    config.navigationMask = 0U;
    return service.begin(config);
}
