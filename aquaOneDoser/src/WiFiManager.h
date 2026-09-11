#pragma once

#include <Arduino.h>
#include <AquaCore/Network/Esp32NetworkBackend.h>
#include <AquaCore/Network/NetworkConfig.h>
#include <AquaCore/Network/NetworkService.h>

class WiFiManager {
public:
    WiFiManager();

    bool begin();
    void loop();
    bool isConnected() const;

    const AquaCore::Network::NetworkService& service() const { return networkService_; }
    AquaCore::Network::NetworkService& service() { return networkService_; }

private:
    AquaCore::Network::Esp32NetworkBackend backend_;
    AquaCore::Network::NetworkService networkService_;
};