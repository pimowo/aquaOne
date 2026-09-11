#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "RuntimeConfig.h"
#include "ConfigValidator.h"
#include "SystemSettings.h"

class ConfigAccessPoint;
class ConfigStore;
class SystemSettingsStore;
class BacklightController;

class ConfigPortal {
public:
  ConfigPortal(const ConfigAccessPoint& accessPoint, ConfigStore& configStore, SystemSettings& systemSettings, SystemSettingsStore& systemSettingsStore, BacklightController& backlight);

  void begin(const RuntimeConfig& config);
  void loop();
  bool isRunning() const { return running_; }

private:
  void handleRoot();
  void handleValidate();
  void handleSave();
  void renderPage();
  void sendPageStart();
  void sendNetworkSection();
  void sendRadioSection();
  void sendTimeSection();
  void sendRemoteSection();
  void sendSystemSection();
  void sendStatus();
  void sendEscaped(const char* value);
  void sendUnsigned(uint32_t value);
  void sendStatic(const char* content);
  void sendInput(const char* name, const char* value, const char* type = "text", bool disabled = false, size_t maxLength = 0);
  void sendNumberInput(const char* name, const char* value, uint32_t minimum, uint32_t maximum);
  void sendCheckbox(const char* name, bool checked);
  bool buildCandidate(RuntimeConfig& candidate, ConfigValidationError& error, uint8_t& radioIndex);
  bool buildSystemSettings(SystemSettings& settings);
  bool copyTextArgument(const char* name, char* target, size_t targetSize, bool preserveEmpty, ConfigValidationError tooLongError, ConfigValidationError& error);
  bool readNumberArgument(const char* name, uint32_t& target, uint32_t minimum, uint32_t maximum, ConfigValidationError rangeError, ConfigValidationError& error);
  const char* errorMessage(ConfigValidationError error, uint8_t radioIndex) const;
  const char* formatValidationError(const ValidationResult& result) const;

  const ConfigAccessPoint& accessPoint_;
  ConfigStore& configStore_;
  SystemSettings& systemSettings_;
  SystemSettingsStore& systemSettingsStore_;
  BacklightController& backlight_;
  const RuntimeConfig* sourceConfig_{nullptr};
  RuntimeConfig workingConfig_{};
  RuntimeConfig candidateConfig_{};
  SystemSettings workingSystemSettings_{};
  WebServer server_{80};
  String* renderBuffer_{nullptr};
  const char* statusMessage_{nullptr};
  bool statusSuccess_{false};
  bool restartPending_{false};
  uint32_t restartScheduledAt_{0};
  bool running_{false};
  mutable char errorBuffer_[128]{};
};
