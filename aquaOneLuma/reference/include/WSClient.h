#pragma once
#include <Arduino.h>
#include "RadioTypes.h"
#include "RuntimeConfig.h"

class WiFiMgr;
class DataStore;

class WSClient {
public:
  WSClient(WiFiMgr& wifi, DataStore& dataStore);
  void begin();
  void connect(const RuntimeRadioConfig& config, uint32_t generation);
  void disconnect();
  void prepareForSleepShutdown();
  void loop();
  RadioConnectionState connectionState() const;
  bool sendCommand(const char* command);
  bool hasFirstData() const;

private:
  void startSession();
  void abortConnectionAttempt();
  bool connectionAttemptTimedOut(uint32_t now) const;
  bool runtimeReconnectTimedOut(uint32_t now) const;
  bool reconnectRetryDue(uint32_t now) const;
  void resetReconnectRetryCadence();
  void startRuntimeReconnect(uint32_t now);
  void stopRuntimeReconnect();
  void invalidateSession();
  void handleEvent(uint8_t type, uint8_t* payload, size_t length,
                   uint32_t callbackSessionId, uint8_t callbackSlot);

  WiFiMgr& wifi_;
  DataStore& dataStore_;
  const RuntimeRadioConfig* config_{nullptr};
  bool started_{false};
  bool sleepShutdownActive_{false};
  bool firstDataReceived_{false};
  bool connectAttemptActive_{false};
  bool connectionAttemptSucceeded_{false};
  bool connectionAttemptAborted_{false};
  RadioConnectionState connectionState_{RADIO_DISABLED};
  uint32_t radioGeneration_{0};
  uint32_t sessionId_{0};
  uint32_t startedAt_{0};
  uint32_t lastSessionStartedAt_{0};
  bool hasSessionStartedAt_{false};
  uint32_t connectAttemptStartedAt_{0};
  bool runtimeReconnectActive_{false};
  uint32_t runtimeReconnectStartedAt_{0};
  uint8_t activeSocketSlot_{0};
  uint8_t nextSocketSlot_{0};
};