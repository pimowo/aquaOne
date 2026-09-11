#pragma once

#include <Arduino.h>
#include "HardwareConfig.h"

class RadioManager;
class WSClient;
struct RuntimeConfig;

class InputManager {
public:
  enum class InputContext : uint8_t { MAIN, VOLUME, RADIO_SELECT };
  enum class UiAction : uint8_t { NONE, OPEN_RADIO_SELECT, RADIO_SELECT_UP, RADIO_SELECT_DOWN, RADIO_SELECT_CONFIRM };

  InputManager(WSClient& wsClient, RadioManager& radioManager, const RuntimeConfig& config);
  void begin();
  void loop();
  void setContext(InputContext context) { context_ = context; }
  bool consumeVolumeScreenRequest();
  UiAction consumeUiAction();

  uint32_t lastActivityMs() const { return lastActivityMs_; }
  void resetActivityTimer(uint32_t now) { lastActivityMs_ = now; }

private:
  enum class ButtonId : uint8_t { PLAY, UP, DOWN };

  struct ButtonState {
    uint8_t pin;
    ButtonId id;
    bool rawPressed{false};
    bool stablePressed{false};
    bool longHandled{false};
    bool volumeContext{false};
    bool volumeInitialSent{false};
    uint32_t rawChangedAt{0};
    uint32_t pressedAt{0};
    uint32_t nextRepeatAt{0};
  };

  static constexpr uint32_t kDebounceMs = 20;
  static constexpr uint32_t kShortMinMs = 50;
  static constexpr uint32_t kLongPressMs = 970;

  void updateButton(ButtonState& button, uint32_t now);
  void handleAction(ButtonId id, bool isLong);
  void handleVolumeRepeat(ButtonId id);
  const char* buttonName(ButtonId id) const;

  WSClient& wsClient_;
  RadioManager& radioManager_;
  const RuntimeConfig& config_;
  ButtonState buttons_[3] = {
      {BUTTON_PLAY_PIN, ButtonId::PLAY},
      {BUTTON_UP_PIN, ButtonId::UP},
      {BUTTON_DOWN_PIN, ButtonId::DOWN},
  };
  InputContext context_{InputContext::MAIN};
  bool volumeScreenRequested_{false};
  bool upDownComboActive_{false};
  UiAction pendingUiAction_{UiAction::NONE};
  uint32_t lastActivityMs_{0};
};
