#pragma once

#include <atomic>
#include <stdint.h>
#include "AquaCore/Network/NetworkBackend.h"

namespace AquaCore {
namespace Network {

class Esp32NetworkBackend final : public NetworkBackend {
public:
    Esp32NetworkBackend();
    ~Esp32NetworkBackend() override;

    bool applyRadioPolicy(
        TriStateSetting persistent,
        TriStateSetting sdkAutoReconnect,
        WifiPowerSaveMode powerSave
    ) override;

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

    NetworkDisconnectReason consumeDisconnectReason() override;

    bool startAccessPoint(
        const char* ssid,
        const char* password
    ) override;

    bool stopAccessPoint() override;
    IpAddress accessPointIp() const override;

private:
    void ensureEventHandler();

    static Esp32NetworkBackend* activeInstance_;
    int eventHandlerId_ = 0;
    std::atomic<uint8_t> pendingEspReason_ {0U};
    std::atomic<bool> hasPendingReason_ {false};
};

} // namespace Network
} // namespace AquaCore
