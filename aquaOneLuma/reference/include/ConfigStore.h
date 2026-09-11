#pragma once

#include <Arduino.h>

struct RuntimeConfig;

class ConfigStore {
public:
  bool load(RuntimeConfig& config);
  bool save(const RuntimeConfig& config);
  bool validate(const RuntimeConfig& config) const;
  bool capturePrimaryForRollback();
  bool restorePrimaryFromRollback();
  void clearRollbackSnapshot();
  bool hasValidConfig() const { return hasValidConfig_; }

private:
  bool hasValidConfig_{false};
};
