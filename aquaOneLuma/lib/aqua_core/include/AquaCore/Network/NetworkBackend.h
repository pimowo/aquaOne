#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AquaCore/Network/NetworkTypes.h"

namespace AquaCore {
namespace Network {

class NetworkBackend {
public:
    virtual ~NetworkBackend() = default;

    virtual bool setHostname(const char* hostname) = 0;
    virtual bool beginSta(
        const char* ssid,
        const char* password
    ) = 0;
    virtual BackendStaState staState() const = 0;
    virtual bool reconnectSta() = 0;
    virtual bool disconnectSta() = 0;
    virtual IpAddress localIp() const = 0;
    virtual int32_t rssi() const = 0;

    virtual bool startAccessPoint(
        const char* ssid,
        const char* password
    ) = 0;
    virtual bool stopAccessPoint() = 0;
    virtual IpAddress accessPointIp() const = 0;
};

} // namespace Network
} // namespace AquaCore
