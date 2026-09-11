#include "FactoryDefaults.h"

#include "ConfigDefaults.h"
#include "RuntimeConfig.h"

#include <string.h>

#include "DebugLog.h"

namespace {
template <size_t N>
void copyBounded(char (&destination)[N], const char* source) {
  if (N == 0) return;
  const char* value = source ? source : "";
  strncpy(destination, value, N - 1);
  destination[N - 1] = '\0';
}
}  // namespace

void FactoryDefaults::load(RuntimeConfig& config) {
  config = RuntimeConfig{};

  // To tylko konfiguracja bootstrapowa; dane sieci i radii użytkownika muszą być odczytane
  // z poprawnego rekordu NVS utworzonego przez ConfigPortal.
  config.useStaticIp = ConfigDefaults::kUseStaticIp;
  copyBounded(config.timezone, ConfigDefaults::kTimezone);
  copyBounded(config.ntpServer, ConfigDefaults::kNtpServer);

  for (uint8_t index = 0; index < YORADIO_MAX; ++index) {
    RuntimeRadioConfig& target = config.radios[index];
    target = RuntimeRadioConfig{};
    target.wsPort = ConfigDefaults::kWebSocketPort;
    copyBounded(target.wsPath, ConfigDefaults::kWebSocketPath);
  }

  config.volumeScreenTimeoutMs = ConfigDefaults::kVolumeScreenTimeoutMs;
  config.volumeRepeatMs = ConfigDefaults::kVolumeRepeatMs;
  config.scrollStartDelayMs = ConfigDefaults::kScrollStartDelayMs;
  config.scrollStepMs = ConfigDefaults::kScrollStepMs;
  config.radioMenuTimeoutMs = ConfigDefaults::kRadioMenuTimeoutMs;
  config.radioConnectingMinMs = ConfigDefaults::kRadioConnectingMinMs;
  config.deepSleepTimeoutSec = ConfigDefaults::kDeepSleepTimeoutSec;
  config.stoppedTimeoutSec = ConfigDefaults::kStoppedTimeoutSec;
  DEBUG_LOGLN("[CONFIG] factory defaults loaded");
}