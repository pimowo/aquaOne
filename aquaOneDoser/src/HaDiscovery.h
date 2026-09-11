#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include "PumpConfig.h"

class PumpManager;

class HaDiscovery {
public:
    void beginPublishing();
    void cancel();
    void loop(PubSubClient& client, const PumpManager& pumps);
    bool isPublishing() const;
    bool isComplete() const;
    uint16_t getPublishedCount() const;
    uint16_t getTotalCount() const;
    size_t getMaxPayloadSize() const;

private:
    static constexpr uint16_t GLOBAL_ENTITY_COUNT = 13;
    static constexpr uint16_t PUMP_ENTITY_COUNT = 24;
    static constexpr uint16_t TOTAL_ENTITY_COUNT = GLOBAL_ENTITY_COUNT + PUMP_COUNT * PUMP_ENTITY_COUNT;

    uint16_t nextEntity = TOTAL_ENTITY_COUNT;
    unsigned long retryNotBefore = 0;
    unsigned long discoveryStartedAt = 0;
    uint8_t retryCount = 0;
    size_t maxPayloadSize = 0;

    bool publishEntity(PubSubClient& client, const PumpManager& pumps, uint16_t index);
    bool publishGlobal(PubSubClient& client, uint16_t index);
    bool publishPump(PubSubClient& client, const PumpManager& pumps,
                     size_t pumpIndex, uint16_t entityIndex);
    bool sendConfig(PubSubClient& client, const char* component, const char* uniqueId,
                    const char* name, const char* stateTopic, const char* commandTopic,
                    const char* extra);
};