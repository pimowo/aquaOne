#include "RadioManager.h"
#include "ConfigValidator.h"
#include "DataStore.h"
#include "WSClient.h"
#include "RuntimeConfig.h"
#include <Preferences.h>

#include "DebugLog.h"
namespace {
constexpr char NVS_NAMESPACE[] = "yostate";
constexpr char LEGACY_NVS_NAMESPACE[] = "yoradio";
constexpr char NVS_LAST_RADIO_ID[] = "last_radio";
}

RadioManager::RadioManager(WSClient& wsClient, DataStore& dataStore, const RuntimeConfig& config)
    : wsClient_(wsClient), dataStore_(dataStore), config_(config) {}

void RadioManager::begin() {
  configValid_ = validateConfig();
  if (!configValid_) {
    Serial.println("[RADIO] configuration invalid; no radio selected");
    return;
  }
  Preferences preferences;
  uint8_t storedId = 0;
  if (preferences.begin(NVS_NAMESPACE, true)) {
    storedId = preferences.getUChar(NVS_LAST_RADIO_ID, 0);
    preferences.end();
  }
  if (storedId == 0 && preferences.begin(LEGACY_NVS_NAMESPACE, true)) {
    storedId = preferences.getUChar(NVS_LAST_RADIO_ID, 0);
    preferences.end();
  }

  const int8_t storedIndex = enabledRadioIndexForId(storedId);
  if (storedIndex >= 0) {
    activateRadio(static_cast<uint8_t>(storedIndex), false);
    return;
  }
  for (uint8_t index = 0; index < YORADIO_MAX; ++index) {
    if (config_.radios[index].enabled) {
      activateRadio(index, false);
      return;
    }
  }
}

bool RadioManager::selectRadio(uint8_t index) {
  return activateRadio(index, true);
}

void RadioManager::loop() {
  if (pendingPersist_ && pendingPersistGeneration_ == generation_ &&
      activeIndex_ >= 0 && wsClient_.hasFirstData()) {
    persistActiveRadio();
    pendingPersist_ = false;
  }
}

bool RadioManager::activateRadio(uint8_t index, bool persistSelection) {
  if (!configValid_ || index >= YORADIO_MAX || !config_.radios[index].enabled) {
    DEBUG_LOGF("[RADIO] selection rejected for index %u\n", index);
    return false;
  }
  const bool changed = activeIndex_ != static_cast<int8_t>(index);
  const bool retryOffline = !changed && wsClient_.connectionState() == RADIO_OFFLINE;
  if (!changed && !retryOffline) return true;

  const RuntimeRadioConfig& selected = config_.radios[index];
  if (++generation_ == 0) ++generation_;
  wsClient_.disconnect();
  activeIndex_ = static_cast<int8_t>(index);
  dataStore_.beginRadio(selected.id, selected.name);
  pendingPersist_ = persistSelection;
  pendingPersistGeneration_ = generation_;
  wsClient_.connect(selected, generation_);
  DEBUG_LOGF("[RADIO][%lu] %s %u: %s (generation %lu)\n", millis(),
             retryOffline ? "retrying" : "selected", selected.id, selected.name, generation_);
  return true;
}

void RadioManager::persistActiveRadio() {
  if (activeIndex_ < 0) return;
  const RuntimeRadioConfig& selected = config_.radios[activeIndex_];
  Preferences preferences;
  if (preferences.begin(NVS_NAMESPACE, false)) {
    preferences.putUChar(NVS_LAST_RADIO_ID, selected.id);
    preferences.end();
    DEBUG_LOGF("[RADIO][%lu] last radio saved: %u\n", millis(), selected.id);
  } else {
    Serial.println("[RADIO] unable to save last radio");
  }
}

int8_t RadioManager::enabledRadioIndexForId(uint8_t id) const {
  if (id == 0) return -1;
  for (uint8_t index = 0; index < YORADIO_MAX; ++index) {
    if (config_.radios[index].enabled && config_.radios[index].id == id) return static_cast<int8_t>(index);
  }
  return -1;
}

bool RadioManager::selectNextEnabled() {
  if (!configValid_) return false;
  const uint8_t first = activeIndex_ < 0 ? 0 : static_cast<uint8_t>((activeIndex_ + 1) % YORADIO_MAX);
  for (uint8_t offset = 0; offset < YORADIO_MAX; ++offset) {
    const uint8_t index = static_cast<uint8_t>((first + offset) % YORADIO_MAX);
    if (config_.radios[index].enabled) return selectRadio(index);
  }
  return false;
}

const RuntimeRadioConfig* RadioManager::activeRadio() const {
  return activeIndex_ >= 0 ? &config_.radios[activeIndex_] : nullptr;
}

uint8_t RadioManager::enabledRadioCount() const {
  uint8_t count = 0;
  for (uint8_t index = 0; index < YORADIO_MAX; ++index) {
    if (config_.radios[index].enabled) ++count;
  }
  return count;
}

int8_t RadioManager::enabledRadioIndexAt(uint8_t enabledPosition) const {
  uint8_t currentPosition = 0;
  for (uint8_t index = 0; index < YORADIO_MAX; ++index) {
    if (!config_.radios[index].enabled) continue;
    if (currentPosition == enabledPosition) return static_cast<int8_t>(index);
    ++currentPosition;
  }
  return -1;
}

const RuntimeRadioConfig* RadioManager::radioAt(uint8_t index) const {
  return index < YORADIO_MAX ? &config_.radios[index] : nullptr;
}

uint8_t RadioManager::currentRadioId() const {
  const RuntimeRadioConfig* radio = activeRadio();
  return radio ? radio->id : 0;
}

RadioConnectionState RadioManager::connectionState() const {
  return activeIndex_ < 0 ? RADIO_DISABLED : wsClient_.connectionState();
}

bool RadioManager::validateConfig() {
  ConfigValidationError error = ConfigValidationError::NONE;
  uint8_t radioIndex = 0;
  if (ConfigValidator::validateRadios(config_, error, radioIndex)) return true;

  if (error == ConfigValidationError::NO_ENABLED_RADIOS) {
    Serial.println("[RADIO] no enabled radio configured");
  } else {
    Serial.printf("[RADIO] invalid radio configuration at index %u (error %u)\n", radioIndex,
                  static_cast<unsigned>(error));
  }
  return false;
}
