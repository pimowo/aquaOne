#pragma once

#include <Arduino.h>
#include "RadioTypes.h"

class WSClient;
class DataStore;
struct RuntimeConfig;
struct RuntimeRadioConfig;

class RadioManager {
public:
  RadioManager(WSClient& wsClient, DataStore& dataStore, const RuntimeConfig& config);
  void begin();
  void loop();
  bool selectRadio(uint8_t index);
  bool selectNextEnabled();
  const RuntimeRadioConfig* activeRadio() const;
  uint8_t enabledRadioCount() const;
  int8_t activeRadioIndex() const { return activeIndex_; }
  int8_t enabledRadioIndexAt(uint8_t enabledPosition) const;
  const RuntimeRadioConfig* radioAt(uint8_t index) const;
  uint8_t currentRadioId() const;
  RadioConnectionState connectionState() const;
  bool validateConfig();

private:
  bool activateRadio(uint8_t index, bool persistSelection);
  void persistActiveRadio();
  int8_t enabledRadioIndexForId(uint8_t id) const;

  WSClient& wsClient_;
  DataStore& dataStore_;
  const RuntimeConfig& config_;
  int8_t activeIndex_{-1};
  uint32_t generation_{0};
  uint32_t pendingPersistGeneration_{0};
  bool pendingPersist_{false};
  bool configValid_{false};
};
