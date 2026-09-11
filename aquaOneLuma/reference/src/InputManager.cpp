#include "InputManager.h"

#include "RadioManager.h"
#include "RuntimeConfig.h"
#include "WSClient.h"


#include "DebugLog.h"
InputManager::InputManager(WSClient& wsClient, RadioManager& radioManager, const RuntimeConfig& config)
    : wsClient_(wsClient), radioManager_(radioManager), config_(config) {}

void InputManager::begin() {
  const uint32_t now = millis();
  for (ButtonState& button : buttons_) {
    pinMode(button.pin, INPUT_PULLUP);
    button.rawPressed = digitalRead(button.pin) == LOW;
    button.stablePressed = button.rawPressed;
    button.rawChangedAt = now;
    button.pressedAt = now;
  }
}

void InputManager::loop() {
  const uint32_t now = millis();
  const bool upPressed = digitalRead(BUTTON_UP_PIN) == LOW;
  const bool downPressed = digitalRead(BUTTON_DOWN_PIN) == LOW;
  if (upPressed && downPressed) {
    upDownComboActive_ = true;
    return;
  }
  if (upDownComboActive_) {
    // Blokujemy oba przyciski, aż gest CONFIG UP+DOWN zostanie całkowicie zwolniony.
    upDownComboActive_ = false;
    for (ButtonState& button : buttons_) {
      const bool pressed = digitalRead(button.pin) == LOW;
      button.rawPressed = pressed;
      button.stablePressed = pressed;
      button.rawChangedAt = now;
      button.longHandled = true;
      button.volumeContext = false;
    }
    return;
  }
  for (ButtonState& button : buttons_) updateButton(button, now);
}

bool InputManager::consumeVolumeScreenRequest() {
  const bool requested = volumeScreenRequested_;
  volumeScreenRequested_ = false;
  return requested;
}

InputManager::UiAction InputManager::consumeUiAction() {
  const UiAction action = pendingUiAction_;
  pendingUiAction_ = UiAction::NONE;
  return action;
}

void InputManager::updateButton(ButtonState& button, uint32_t now) {
  const bool rawPressed = digitalRead(button.pin) == LOW;
  if (rawPressed != button.rawPressed) {
    button.rawPressed = rawPressed;
    button.rawChangedAt = now;
  }

  if (button.stablePressed != button.rawPressed && now - button.rawChangedAt >= kDebounceMs) {
    button.stablePressed = button.rawPressed;
    if (button.stablePressed) {
      button.pressedAt = now;
      button.longHandled = false;
      button.volumeContext = (button.id != ButtonId::PLAY && context_ == InputContext::VOLUME);
      button.volumeInitialSent = false;
      button.nextRepeatAt = 0;
    } else if (button.volumeContext) {
      const uint32_t heldMs = now - button.pressedAt;
      if (!button.volumeInitialSent && heldMs >= kShortMinMs) handleAction(button.id, false);
      button.volumeContext = false;
      button.volumeInitialSent = false;
    } else if (!button.longHandled) {
      const uint32_t heldMs = now - button.pressedAt;
      if (heldMs >= kShortMinMs && heldMs < kLongPressMs) handleAction(button.id, false);
    }
  }

  if (button.stablePressed && button.volumeContext) {
    const uint32_t heldMs = now - button.pressedAt;
    if (!button.volumeInitialSent && heldMs >= kShortMinMs) {
      button.volumeInitialSent = true;
      button.nextRepeatAt = now + config_.volumeRepeatMs;
      handleAction(button.id, false);
    } else if (button.volumeInitialSent &&
               static_cast<int32_t>(now - button.nextRepeatAt) >= 0) {
      button.nextRepeatAt = now + config_.volumeRepeatMs;
      handleVolumeRepeat(button.id);
    }
    return;
  }

  if (button.stablePressed && !button.longHandled && now - button.pressedAt >= kLongPressMs) {
    button.longHandled = true;
    handleAction(button.id, true);
  }
}

void InputManager::handleAction(ButtonId id, bool isLong) {
  const uint32_t now = millis();
  lastActivityMs_ = now;
  DEBUG_LOGF("[INPUT][%lu] %s %s\n", now, buttonName(id), isLong ? "long" : "short");

  if (context_ == InputContext::RADIO_SELECT) {
    if (id == ButtonId::UP) pendingUiAction_ = UiAction::RADIO_SELECT_UP;
    else if (id == ButtonId::DOWN) pendingUiAction_ = UiAction::RADIO_SELECT_DOWN;
    else if (!isLong) pendingUiAction_ = UiAction::RADIO_SELECT_CONFIRM;
    return;
  }

  if (id == ButtonId::PLAY && isLong) {
    pendingUiAction_ = UiAction::OPEN_RADIO_SELECT;
    DEBUG_LOGF("[INPUT][%lu] radio select requested\n", now);
    return;
  }

  if (id == ButtonId::PLAY && !isLong && radioManager_.enabledRadioCount() == 1 &&
      radioManager_.connectionState() == RADIO_OFFLINE) {
    const int8_t activeIndex = radioManager_.activeRadioIndex();
    if (activeIndex >= 0) {
      radioManager_.selectRadio(static_cast<uint8_t>(activeIndex));
      DEBUG_LOGF("[INPUT][%lu] offline radio retry requested\n", now);
    }
    return;
  }

  const char* command = nullptr;
  if (id == ButtonId::PLAY) command = "toggle=1";
  else if (id == ButtonId::UP) command = isLong ? "next=1" : "volp=1";
  else command = isLong ? "prev=1" : "volm=1";

  if (wsClient_.sendCommand(command)) {
    DEBUG_LOGF("[INPUT][%lu] command %s\n", now, command);
    if (!isLong && (id == ButtonId::UP || id == ButtonId::DOWN)) volumeScreenRequested_ = true;
  } else {
    DEBUG_LOGF("[INPUT][%lu] command ignored: WS offline (%s)\n", now, command);
  }
}

void InputManager::handleVolumeRepeat(ButtonId id) {
  const uint32_t now = millis();
  const char* command = id == ButtonId::UP ? "volp=1" : "volm=1";
  lastActivityMs_ = now;
  if (wsClient_.sendCommand(command)) {
    volumeScreenRequested_ = true;
    DEBUG_LOGF("[INPUT][%lu] %s volume repeat (%s)\n", now, buttonName(id), command);
  } else {
    DEBUG_LOGF("[INPUT][%lu] volume repeat ignored: WS offline (%s)\n", now, command);
  }
}

const char* InputManager::buttonName(ButtonId id) const {
  switch (id) {
    case ButtonId::PLAY: return "PLAY";
    case ButtonId::UP: return "UP";
    case ButtonId::DOWN: return "DOWN";
  }
  return "UNKNOWN";
}
