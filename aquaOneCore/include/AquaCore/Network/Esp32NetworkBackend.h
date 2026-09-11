#pragma once

#include "AquaCore/Network/NetworkBackend.h"

namespace AquaCore {
namespace Network {

class Esp32NetworkBackend final : public NetworkBackend {
public:
    bool setHostname(const char* hostname) override;

    bool beginSta(
        const char* ssid,
        const char* password
    ) override;

    BackendStaState staState() const override;
    bool reconnectSta() override;
    bool disconnectSta() override;
    IpAddress localIp() const override;
    int32_t rssi() const override;

    bool startAccessPoint(
        const char* ssid,
        const char* password
    ) override;

    bool stopAccessPoint() override;
    IpAddress accessPointIp() const override;
};

} // namespace Network
} // namespace AquaCore
