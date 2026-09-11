#include "UIRenderer.h"
#include <WiFi.h>

#include "ClockService.h"
#include "BatteryMonitor.h"
#include "DataStore.h"
#include "DisplayDriver.h"
#include "RadioManager.h"
#include "PowerStatusMonitor.h"
#include "RuntimeConfig.h"
#include "WiFiMgr.h"
#include "WSClient.h"
#include "YoRadioFont.h"
#include "HardwareConfig.h"

namespace {
constexpr uint16_t BLACK = 0x0000, WHITE = 0xFFFF, YELLOW = 0xFFE0, GREEN = 0x07E0;
constexpr uint16_t BLUE = 0x001F, RED = 0xF800;
constexpr uint32_t STARTUP_ANIMATION_MS = 350, MAIN_REFRESH_MS = 100;
constexpr uint32_t CLOCK_BLINK_MS = 500;
constexpr int16_t SCROLL_DELTA_PX = 2;
constexpr uint8_t GLYPH_ADVANCE = 6;
constexpr int16_t CLOCK_X = 13, CLOCK_Y = 66, CLOCK_COLON_X = 37, CLOCK_AREA_Y = 64;
constexpr uint16_t CLOCK_COLON_W = 12, CLOCK_AREA_H = 18;
constexpr int16_t STATUS_AREA_Y = 100;
constexpr uint16_t STATUS_AREA_H = 17;

struct MediaLineLayout {
  int16_t screenY;
  uint8_t height;
  int16_t glyphY;
  uint8_t scale;
  uint16_t background;
  uint16_t foreground;
};

constexpr MediaLineLayout MEDIA_LINES[] = {
    {4, 18, 1, 2, WHITE, BLACK},
    {27, 18, 1, 2, BLACK, WHITE},
    {48, 8, 0, 1, BLACK, WHITE},
};
constexpr uint16_t TEXT_X = 4;
constexpr uint16_t TEXT_AREA_WIDTH = 120;
}

UIRenderer::UIRenderer(DisplayDriver& display, WiFiMgr& wifi, WSClient& ws,
                       DataStore& dataStore, RadioManager& radios, ClockService& clock,
                       BatteryMonitor& battery, PowerStatusMonitor& powerStatus,
                       const RuntimeConfig& config)
    : display_(display), wifi_(wifi), ws_(ws), dataStore_(dataStore), radios_(radios),
      clock_(clock), battery_(battery), powerStatus_(powerStatus), config_(config) {}
void UIRenderer::begin() {
  screen_ = Screen::WIFI_CONNECTING;
  lastAnimationAt_ = millis();
  lastClockBlinkAt_ = lastAnimationAt_;
  dots_ = 1;
  renderStartup(true);
}

void UIRenderer::showVolumeScreen() {
  volumeScreenUntil_ = millis() + config_.volumeScreenTimeoutMs;
  if (screen_ != Screen::VOLUME) volumeScreenDrawn_ = false;
}

bool UIRenderer::isVolumeScreenActive() const {
  return volumeScreenUntil_ != 0 && static_cast<int32_t>(millis() - volumeScreenUntil_) < 0;
}

bool UIRenderer::openRadioSelect() {
  if (radios_.enabledRadioCount() <= 1) return false;
  radioSelectIndex_ = radios_.activeRadioIndex();
  radioSelectUntil_ = millis() + config_.radioMenuTimeoutMs;
  return true;
}

bool UIRenderer::isRadioSelectActive() const {
  return radioSelectUntil_ != 0 && static_cast<int32_t>(millis() - radioSelectUntil_) < 0;
}

void UIRenderer::moveRadioSelection(int8_t direction) {
  const uint8_t count = radios_.enabledRadioCount();
  if (!isRadioSelectActive() || count == 0 || direction == 0) return;
  // KaĹĽde poprawnie rozpoznane UP/DOWN przedĹ‚uĹĽa menu, takĹĽe na jego granicy.
  radioSelectUntil_ = millis() + config_.radioMenuTimeoutMs;
  uint8_t position = 0;
  for (; position < count; ++position) {
    if (radios_.enabledRadioIndexAt(position) == radioSelectIndex_) break;
  }
  if (direction > 0) {
    if (position + 1 >= count) return;
    ++position;
  } else {
    if (position == 0) return;
    --position;
  }
  const int8_t previousIndex = radioSelectIndex_;
  radioSelectIndex_ = radios_.enabledRadioIndexAt(position);
 drawRadioSelectRow(previousIndex);
  drawRadioSelectRow(radioSelectIndex_);
  drawRadioSelectIndicators(false);
}

void UIRenderer::confirmRadioSelection() {
  if (!isRadioSelectActive() || radioSelectIndex_ < 0) return;
  confirmedRadioIndex_ = radioSelectIndex_;
  radioSelectUntil_ = 0;
}

int8_t UIRenderer::consumeRadioSelection() {
  const int8_t selection = confirmedRadioIndex_;
  confirmedRadioIndex_ = -1;
  return selection;
}

void UIRenderer::loop() {

  const uint32_t now = millis();
  if (volumeScreenUntil_ != 0 && static_cast<int32_t>(now - volumeScreenUntil_) >= 0) {
    volumeScreenUntil_ = 0;
    volumeScreenDrawn_ = false;
  }
  if (radioSelectUntil_ != 0 && static_cast<int32_t>(now - radioSelectUntil_) >= 0) {
    radioSelectUntil_ = 0;
  }

  Screen desired = Screen::WIFI_CONNECTING;
  if (isRadioSelectActive()) {
    desired = Screen::RADIO_SELECT;
  } else if (ws_.connectionState() == RADIO_OFFLINE) {
    desired = Screen::WS_ERROR;
  } else if (!wifi_.isConnected()) {
    desired = reachedMain_ ? Screen::WS_ERROR : Screen::WIFI_CONNECTING;
  } else if (!radios_.activeRadio()) {
    desired = Screen::WS_ERROR;
  } else {
    if (screen_ == Screen::WIFI_CONNECTING) desired = Screen::RADIO_CONNECTING;
    else if (ws_.hasFirstData() &&
             static_cast<uint32_t>(now - radioConnectingSince_) >= config_.radioConnectingMinMs) {
      if (ws_.connectionState() != RADIO_ONLINE) desired = Screen::WS_ERROR;
      else desired = isVolumeScreenActive() ? Screen::VOLUME : Screen::MAIN;
    }
    else if (ws_.hasFirstData()) desired = Screen::RADIO_CONNECTING;
    else desired = Screen::RADIO_CONNECTING;
  }
  if (desired != screen_) {
    screen_ = desired;
    if (screen_ == Screen::MAIN || screen_ == Screen::WS_ERROR) {
      if (screen_ == Screen::MAIN) reachedMain_ = true;
      invalidateMainCache();
      renderMain(true);
    } else if (screen_ == Screen::VOLUME) {
      volumeScreenDrawn_ = false;
      renderVolumeScreen(true);
    } else if (screen_ == Screen::RADIO_SELECT) {
      renderRadioSelect(true);
    } else {
      if (screen_ == Screen::RADIO_CONNECTING) radioConnectingSince_ = now;
      dots_ = 1;
      lastAnimationAt_ = now;
      renderStartup(true);
    }
    return;
  }

  if (screen_ == Screen::VOLUME) {
    renderVolumeScreen(false);
    return;
  }

  if (screen_ == Screen::RADIO_SELECT) return;

  if (screen_ == Screen::WIFI_CONNECTING || screen_ == Screen::RADIO_CONNECTING) {
    if (now - lastAnimationAt_ >= STARTUP_ANIMATION_MS) {
      lastAnimationAt_ = now;
      dots_ = dots_ == 3 ? 1 : static_cast<uint8_t>(dots_ + 1);
      renderStartup(false);
    }
    return;
  }

  if (now - lastMainCheckAt_ >= MAIN_REFRESH_MS) {
    lastMainCheckAt_ = now;
    renderMain(false);
  }
  updateScroll();
}

void UIRenderer::renderStartup(bool force) {
  const bool radio = screen_ == Screen::RADIO_CONNECTING;
  const String target = radio ? dataStore_.radioName() : String(config_.wifiSsid);
  if (force) {
    display_.fillScreen(BLACK);
    drawText(43, 4, "yo", 1, WHITE);
    drawText(55, 4, "PILOT", 1, GREEN);
    drawCentered(48, String(u8"\u0141\u0105cz\u0119 z"), 1, WHITE, 0);
    drawCentered(66, target, 2, radio ? GREEN : WHITE, 10);
  }
  display_.fillRect(44, 88, 40, 14, BLACK);
  String dots;
  for (uint8_t i = 0; i < dots_; ++i) dots += '.';
  drawCentered(88, dots, 2, WHITE, 3);
}

void UIRenderer::renderMain(bool force) {
  const bool error = screen_ == Screen::WS_ERROR;
  const String station = error ? String("") : dataStore_.station();
  const String artist = error ? String("") : dataStore_.artist();
  const String title = error ? String("") : dataStore_.title();
  const String volume = error ? String("") : dataStore_.volume();
  if (force) display_.fillScreen(BLACK);

  const String media[] = {station, artist, title};
  for (uint8_t line = 0; line < 3; ++line) {
    const String& cached = line == 0 ? lastStation_ : line == 1 ? lastArtist_ : lastTitle_;
    if (force || error != lastErrorMode_ || media[line] != cached) {
      setScrollText(line, media[line]);
      renderMediaLine(line);
      if (line == 0) lastStation_ = media[line];
      else if (line == 1) lastArtist_ = media[line];
      else lastTitle_ = media[line];
    }
  }

  drawClock(force);
  drawBitrateBox(force);
  const bool batteryKnown = battery_.hasReading();
  const uint8_t batteryPercent = battery_.percent();
  if (force || batteryKnown != lastBatteryKnown_ ||
      (batteryKnown && batteryPercent != lastBatteryPercent_)) {
    drawBattery();
    lastBatteryKnown_ = batteryKnown;
    lastBatteryPercent_ = batteryPercent;
  }
  drawPowerIndicators(force);

  

  if (force || error != lastErrorMode_ || volume != lastVolume_) {
    display_.fillRect(0, STATUS_AREA_Y, 48, STATUS_AREA_H, BLACK);
    drawSpeakerGlyph(4, 102);
    if (volume.length()) drawText(17, 104, volume, 1, YELLOW, 6);
    lastVolume_ = volume;
  }
  drawRssi(force, error);

  String bottomText;
  uint16_t bottomColor = error ? RED : GREEN;
  if (error) {
    bottomText = errorLabel();
  } else if (radios_.enabledRadioCount() > 1) {
    bottomText = dataStore_.radioName();
  } else {
    const String state = dataStore_.playerState();
    if (state == "stop" || state == "stopped" || state == "paused") {
      bottomText = u8"Zatrzymany";
    } else if (state == "connecting" || !ws_.hasFirstData() ||
               ws_.connectionState() == RADIO_WS_CONNECTING) {
      bottomText = String(u8"\u0141\u0105czenie");
    } else if (ws_.connectionState() == RADIO_ONLINE) {
      bottomText = "Gra";
    }
  }
  if (force || error != lastErrorMode_ || bottomText != lastRadioName_ ||
      bottomColor != lastBottomColor_) {
    display_.fillRect(0, 117, DISPLAY_WIDTH, 11, BLACK);
    drawText(4, 118, "Radio:", 1, WHITE);
    if (bottomText.length()) drawText(42, 118, bottomText, 1, bottomColor, 14);
    lastRadioName_ = bottomText;
    lastBottomColor_ = bottomColor;
  }
  lastErrorMode_ = error;
}

void UIRenderer::drawClock(bool force) {
  char buffer[8];
  clock_.format(buffer, sizeof(buffer), true);
  const String value(buffer);
  if (force || value != lastClock_) {
    display_.fillRect(7, CLOCK_AREA_Y, 72, CLOCK_AREA_H, BLACK);
    char visibleBuffer[8];
    clock_.format(visibleBuffer, sizeof(visibleBuffer), clockColonVisible_);
    drawText(CLOCK_X, CLOCK_Y, String(visibleBuffer), 2, WHITE);
    lastClock_ = value;
  }


  const uint32_t now = millis();
  if (now - lastClockBlinkAt_ < CLOCK_BLINK_MS) return;
  lastClockBlinkAt_ = now;
  clockColonVisible_ = !clockColonVisible_;
  display_.fillRect(CLOCK_COLON_X, CLOCK_AREA_Y, CLOCK_COLON_W, CLOCK_AREA_H, BLACK);
  if (clockColonVisible_) drawText(CLOCK_COLON_X, CLOCK_Y, ":", 2, WHITE);
}

void UIRenderer::drawRssi(bool force, bool error) {
  constexpr int16_t labelX = 43, valueX = 80, unitX = 104;
  constexpr uint16_t labelW = 37, valueW = 24;
  constexpr uint32_t ROTATE_MS = 2500;
  const uint32_t now = millis();
  const RssiSource wanted = ((now / ROTATE_MS) & 1U) ? RssiSource::PILOT : RssiSource::RADIO;
  const bool sourceChanged = wanted != rssiSource_;
  if (sourceChanged) {
    rssiSource_ = wanted;
}
  const bool errorChanged = error != lastErrorMode_;
  if (error) {
    if (force || errorChanged) {
      display_.fillRect(labelX, STATUS_AREA_Y, DISPLAY_WIDTH - labelX, STATUS_AREA_H, BLACK);
    }
    return;
  }

  int value = INT32_MIN;
  RssiZone* zone = nullptr;
  const char* label = rssiSource_ == RssiSource::RADIO ? "RSSI-R" : "RSSI-P";
  if (rssiSource_ == RssiSource::RADIO) {
    const int incoming = dataStore_.rssi();
    if (incoming >= -120 && incoming <= -1 &&
        (lastRssi_ == INT32_MIN || now - lastRssiDrawAt_ >= 15000)) {
      lastRssi_ = incoming;
      lastRssiDrawAt_ = now;
    }
    value = lastRssi_;
    zone = &radioRssiZone_;
  } else {
    if (WiFi.status() == WL_CONNECTED) {
      if (lastPilotRssi_ == INT32_MIN || now - lastPilotRssiReadAt_ >= 15000) {
        const int incoming = WiFi.RSSI();
        lastPilotRssiReadAt_ = now;
        if (incoming >= -120 && incoming <= -1) {
          lastPilotRssi_ = incoming;
        } else {
          lastPilotRssi_ = INT32_MIN;
          pilotRssiZone_ = RssiZone::UNKNOWN;
        }
      }
    } else {
      lastPilotRssi_ = INT32_MIN;
      pilotRssiZone_ = RssiZone::UNKNOWN;
    }
    value = lastPilotRssi_;
    zone = &pilotRssiZone_;
  }

  const bool valid = value >= -120 && value <= -1;
  const bool firstValid = valid && *zone == RssiZone::UNKNOWN;
  RssiZone next = *zone;
  // Histereza ogranicza zmianÄ™ koloru przy RSSI blisko granic stref.
  if (valid) {
    if (next == RssiZone::UNKNOWN) {
      next = value >= -67 ? RssiZone::GREEN : (value >= -75 ? RssiZone::YELLOW : RssiZone::RED);
    } else if (next == RssiZone::GREEN) {
      next = value < -70 ? RssiZone::YELLOW : RssiZone::GREEN;
    } else if (next == RssiZone::YELLOW) {
      next = value >= -67 ? RssiZone::GREEN : (value < -78 ? RssiZone::RED : RssiZone::YELLOW);
    } else if (next == RssiZone::RED) {
      next = value >= -75 ? RssiZone::YELLOW : RssiZone::RED;
    }
  }
  const bool zoneChanged = valid && next != *zone;
  const uint16_t color = next == RssiZone::GREEN ? GREEN :
                         (next == RssiZone::YELLOW ? YELLOW : RED);

  if (force || errorChanged) {
    display_.fillRect(labelX, STATUS_AREA_Y, DISPLAY_WIDTH - labelX, STATUS_AREA_H, BLACK);
    rssiUnitDrawn_ = false;
    renderedRssi_ = INT32_MIN;
  }
  if (force || errorChanged || sourceChanged || firstValid || zoneChanged) {
    display_.fillRect(labelX, STATUS_AREA_Y, labelW, STATUS_AREA_H, BLACK);
    drawText(labelX, 104, label, 1, valid ? color : WHITE);
  }

  const uint16_t unitColor = valid ? color : WHITE;
  if (!rssiUnitDrawn_ || lastRssiUnitColor_ != unitColor || firstValid || zoneChanged) {
    drawText(unitX, 104, "dBm", 1, unitColor);
    rssiUnitDrawn_ = true;
    lastRssiUnitColor_ = unitColor;
  }
  if (!valid) {
    if (renderedRssi_ != INT32_MIN) {
      display_.fillRect(valueX, STATUS_AREA_Y, valueW, STATUS_AREA_H, BLACK);
      renderedRssi_ = INT32_MIN;
    }
    return;
  }

  if (value != renderedRssi_ || firstValid || zoneChanged) {
    display_.fillRect(valueX, STATUS_AREA_Y, valueW, STATUS_AREA_H, BLACK);
    const String text = encodeText(String(value), 0);
    drawEncodedText(unitX - textWidth(text, 1), 104, text, 1, color);
    renderedRssi_ = value;
  }
  *zone = next;
}

void UIRenderer::renderVolumeScreen(bool force) {
  const String value = dataStore_.volume();
  if (force || !volumeScreenDrawn_) {
    display_.fillScreen(BLACK);
    drawCentered(47, String(u8"G\u0142o\u015bno\u015b\u0107"), 1, WHITE);
    if (value.length()) drawCentered(65, value, 2, YELLOW);
    drawText(116, 12, "+", 2, WHITE);
    drawText(116, 100, "-", 2, WHITE);
    if (wifi_.isConnected()) {
      const IPAddress ip = WiFi.localIP();
      if (ip != IPAddress(0, 0, 0, 0)) drawCentered(116, String("IP: ") + ip.toString(), 1, WHITE);
    }
    volumeScreenDrawn_ = true;
    lastVolumeScreenValue_ = value;
    return;
  }
  if (value == lastVolumeScreenValue_) return;
  display_.fillRect(40, 65, 48, 16, BLACK);
  if (value.length()) drawCentered(65, value, 2, YELLOW);
  lastVolumeScreenValue_ = value;
}

void UIRenderer::renderRadioSelect(bool force) {
  if (!force) return;
  display_.fillScreen(BLACK);
  drawCentered(10, "Wybierz radio", 1, WHITE);
  const uint8_t count = radios_.enabledRadioCount();
  for (uint8_t position = 0; position < count && position < YORADIO_MAX; ++position) {
    const int8_t index = radios_.enabledRadioIndexAt(position);
    drawRadioSelectRow(index);
  }
  drawRadioSelectIndicators(true);
}

void UIRenderer::drawRadioSelectRow(int8_t index) {
  const uint8_t count = radios_.enabledRadioCount();
  for (uint8_t position = 0; position < count && position < YORADIO_MAX; ++position) {
    if (radios_.enabledRadioIndexAt(position) != index) continue;
    const RuntimeRadioConfig* radio = radios_.radioAt(static_cast<uint8_t>(index));
    if (!radio) return;
    const int16_t y = 24 + position * 11;
    const bool selected = index == radioSelectIndex_;
    const bool active = index == radios_.activeRadioIndex();
    display_.fillRect(10, y - 1, 108, 10, selected ? GREEN : BLACK);
    drawText(14, y, active ? String("*") : String(" "), 1, selected ? BLACK : WHITE);
    drawText(26, y, String(radio->name), 1, selected ? BLACK : WHITE, 15);
    return;
  }
}

void UIRenderer::drawRadioSelectIndicators(bool force) {
  constexpr int16_t arrowX = 118;
  constexpr int16_t upY = 3;
  constexpr int16_t downY = 118;
  constexpr uint16_t arrowSize = 7;
  constexpr int16_t enterX = 3;
  constexpr int16_t enterY = 118;
  constexpr uint16_t enterW = 8;
  constexpr uint16_t enterH = 7;

  const uint8_t count = radios_.enabledRadioCount();
  uint8_t position = 0;
  for (; position < count; ++position) {
    if (radios_.enabledRadioIndexAt(position) == radioSelectIndex_) break;
  }
  const bool upVisible = position > 0 && position < count;
  const bool downVisible = position + 1 < count;

  if (force || upVisible != radioSelectUpVisible_) {
    display_.fillRect(arrowX, upY, arrowSize, arrowSize, BLACK);
    if (upVisible) {
      display_.fillRect(arrowX + 3, upY, 1, 1, WHITE);
      display_.fillRect(arrowX + 2, upY + 1, 3, 1, WHITE);
      display_.fillRect(arrowX + 1, upY + 2, 5, 1, WHITE);
      display_.fillRect(arrowX, upY + 3, 7, 1, WHITE);
      display_.fillRect(arrowX + 3, upY + 4, 1, 3, WHITE);
    }
    radioSelectUpVisible_ = upVisible;
  }
  if (force || downVisible != radioSelectDownVisible_) {
    display_.fillRect(arrowX, downY, arrowSize, arrowSize, BLACK);
    if (downVisible) {
      display_.fillRect(arrowX + 3, downY, 1, 3, WHITE);
      display_.fillRect(arrowX, downY + 3, 7, 1, WHITE);
      display_.fillRect(arrowX + 1, downY + 4, 5, 1, WHITE);
      display_.fillRect(arrowX + 2, downY + 5, 3, 1, WHITE);
      display_.fillRect(arrowX + 3, downY + 6, 1, 1, WHITE);
    }
    radioSelectDownVisible_ = downVisible;
  }
  if (force) {
    display_.fillRect(enterX, enterY, enterW, enterH, BLACK);
    display_.fillRect(enterX + 6, enterY, 1, 5, WHITE);
    display_.fillRect(enterX + 2, enterY + 4, 5, 1, WHITE);
    display_.fillRect(enterX + 3, enterY + 3, 1, 1, WHITE);
    display_.fillRect(enterX + 3, enterY + 5, 1, 1, WHITE);
    display_.fillRect(enterX + 4, enterY + 2, 1, 1, WHITE);
    display_.fillRect(enterX + 4, enterY + 6, 1, 1, WHITE);
  }
}
const char* UIRenderer::errorLabel() const {
  if (!wifi_.isConnected()) return u8"BRAK WI-FI";
  if (!radios_.activeRadio()) return u8"BRAK RADIA";
  if (ws_.connectionState() == RADIO_WS_ERROR) return u8"BÄ‚â€žĂ„â€¦Ä‚â€šĂ‚ÂĂ„â€šĂ˘â‚¬ĹľÄ‚ËĂ˘â€šÂ¬ÄąÄľD WS";
  return u8"RADIO OFFLINE";
}

void UIRenderer::drawBattery() {
  // CaĹ‚a grupa baterii jest optycznie wyĹ›rodkowana pod grupÄ… zegara.
  constexpr int16_t bodyX = 10;
  constexpr int16_t bodyY = 88;
  display_.fillRect(9, 86, 64, 10, BLACK);
  display_.fillRect(bodyX, bodyY, 30, 1, WHITE);
  display_.fillRect(bodyX, bodyY + 7, 30, 1, WHITE);
  display_.fillRect(bodyX, bodyY, 1, 8, WHITE);
  display_.fillRect(bodyX + 29, bodyY, 1, 8, WHITE);
  display_.fillRect(bodyX + 30, bodyY + 2, 2, 4, WHITE);

  if (!battery_.hasReading()) return;

  const uint8_t fillWidth = static_cast<uint8_t>((26UL * battery_.percent()) / 100UL);
  if (fillWidth > 0) {
    uint16_t fillColor = RED;
    if (battery_.millivolts() >= 3850) fillColor = GREEN;
    else if (battery_.millivolts() >= 3550) fillColor = YELLOW;
    display_.fillRect(bodyX + 2, bodyY + 2, fillWidth, 4, fillColor);
  }

  char percentText[6];
  snprintf(percentText, sizeof(percentText), "%u%%", battery_.percent());
  drawText(49, 88, String(percentText), 1, WHITE);
}

void UIRenderer::drawPowerIndicators(bool force) {
  const bool usb = powerStatus_.usbPresent();
  const bool charging = powerStatus_.charging();
  const PowerIconState next = !usb ? PowerIconState::NONE
                                   : (charging ? PowerIconState::CHARGING
                                               : PowerIconState::PLUG);
  if (!force && next == powerIconState_) return;

  constexpr int16_t iconX = 75;  // Pozycja wtyczki 76 przesuniÄ™ta o 1 px w lewo.
  constexpr int16_t iconY = 87;
  constexpr uint16_t iconW = 7;
  display_.fillRect(iconX, 86, iconW, 10, BLACK);

  if (next == PowerIconState::PLUG) {
    display_.fillRect(iconX + 2, iconY, 3, 2, WHITE);
    display_.fillRect(iconX + 1, iconY + 2, 5, 4, WHITE);
    display_.fillRect(iconX + 2, iconY + 6, 3, 2, WHITE);
    display_.fillRect(iconX + 3, iconY + 8, 1, 1, WHITE);
  } else if (next == PowerIconState::CHARGING) {
    display_.fillRect(iconX + 3, iconY, 2, 2, YELLOW);
    display_.fillRect(iconX + 2, iconY + 2, 2, 2, YELLOW);
    display_.fillRect(iconX + 3, iconY + 4, 2, 2, YELLOW);
    display_.fillRect(iconX + 2, iconY + 6, 2, 2, YELLOW);
  }
  powerIconState_ = next;
}
void UIRenderer::drawBitrateBox(bool force) {
  const String bitrate = dataStore_.bitrate();
  const String format = dataStore_.format();
  const String state = dataStore_.playerState();
  const bool visible = ws_.connectionState() == RADIO_ONLINE && ws_.hasFirstData() &&
                       state != "stop" && state != "stopped" && state != "paused" &&
                       state != "connecting" && state != "error";
  if (!force && bitrate == lastBitrate_ && format == lastFormat_ &&
      visible == lastBitrateVisible_) return;
  constexpr int16_t x = 88, y = 64, w = 34, h = 32;
  display_.fillRect(x, y, w, h, BLACK);
  if (visible && (bitrate.length() || format.length())) {
    display_.fillRect(x, y, w, 1, WHITE);
    display_.fillRect(x, y + h - 1, w, 1, WHITE);
    display_.fillRect(x, y, 1, h, WHITE);
    display_.fillRect(x + w - 1, y, 1, h, WHITE);
    display_.fillRect(x + 1, y + 15, w - 2, 1, WHITE);
    if (bitrate.length()) { const String text = encodeText(bitrate, 5); drawEncodedText(x + (w - textWidth(text, 1)) / 2, y + 4, text, 1, WHITE); }
    if (format.length()) { const String text = encodeText(format, 5); display_.fillRect(x + 1, y + 16, w - 2, h - 17, WHITE); drawEncodedText(x + (w - textWidth(text, 1)) / 2, y + 19, text, 1, BLACK); }
  }
  lastBitrate_ = bitrate;
  lastFormat_ = format;
  lastBitrateVisible_ = visible;
}

void UIRenderer::setScrollText(uint8_t lineIndex, const String& text) {
  ScrollLine& line = scrollLines_[lineIndex];
  const String encoded = encodeText(text, 0);
  if (line.text == encoded) return;
  line.text = encoded;
  line.textWidth = textWidth(encoded, MEDIA_LINES[lineIndex].scale);
  resetScroll(lineIndex);
}

void UIRenderer::resetScroll(uint8_t lineIndex) {
  ScrollLine& line = scrollLines_[lineIndex];
  line.offset = 0;
  line.lastStepAt = millis();
  line.started = false;
}

bool UIRenderer::lineNeedsScroll(uint8_t lineIndex) const {
  return scrollLines_[lineIndex].textWidth > TEXT_AREA_WIDTH;
}

bool UIRenderer::updateActiveScroll() {
  const uint8_t index = static_cast<uint8_t>(activeScroll_);
  ScrollLine& line = scrollLines_[index];
  if (!lineNeedsScroll(index)) {
    activeScroll_ = -1;
    nextScrollCandidate_ = static_cast<uint8_t>((index + 1) % 3);
    return false;
  }

  const uint32_t now = millis();
  if (!line.started) {
    if (now - line.lastStepAt < config_.scrollStartDelayMs) return false;
    line.started = true;
    line.lastStepAt = now;
    return true;
  }
  if (now - line.lastStepAt < config_.scrollStepMs) return false;
  line.lastStepAt = now;
  line.offset -= SCROLL_DELTA_PX;
  const uint16_t separatorWidth = 3 * GLYPH_ADVANCE * MEDIA_LINES[index].scale;
  if (-line.offset > static_cast<int32_t>(line.textWidth + separatorWidth)) {
    line.offset = 0;
    line.started = false;
    activeScroll_ = -1;
    nextScrollCandidate_ = static_cast<uint8_t>((index + 1) % 3);
  }
  return true;
}

void UIRenderer::updateScroll() {
  if (activeScroll_ >= 0) {
    const uint8_t index = static_cast<uint8_t>(activeScroll_);
    if (updateActiveScroll()) renderMediaLine(index);
    return;
  }
  for (uint8_t offset = 0; offset < 3; ++offset) {
    const uint8_t candidate = static_cast<uint8_t>((nextScrollCandidate_ + offset) % 3);
    if (lineNeedsScroll(candidate)) {
      activeScroll_ = static_cast<int8_t>(candidate);
      resetScroll(candidate);
      return;
    }
  }
}

void UIRenderer::renderMediaLine(uint8_t lineIndex) {
  const MediaLineLayout& layout = MEDIA_LINES[lineIndex];
  const uint32_t pixelCount = static_cast<uint32_t>(DISPLAY_WIDTH) * layout.height;
  for (uint32_t i = 0; i < pixelCount; ++i) lineBuffer_[i] = layout.background;
  const ScrollLine& line = scrollLines_[lineIndex];
  String visible = line.text;
  if (lineNeedsScroll(lineIndex)) visible += " * " + line.text;
  drawEncodedTextToLine(visible, TEXT_X + line.offset, layout.glyphY, layout.scale,
                        layout.foreground, layout.height);
  display_.pushRect(0, layout.screenY, DISPLAY_WIDTH, layout.height, lineBuffer_);
}

void UIRenderer::drawEncodedTextToLine(const String& text, int16_t x, int16_t y,
                                       uint8_t scale, uint16_t color, uint8_t lineHeight) {
  for (size_t i = 0; i < text.length(); ++i) {
    const YoRadioGlyph& glyph = yoRadioGlyph(static_cast<uint8_t>(text[i]));
    for (uint8_t col = 0; col < 5; ++col) {
      for (uint8_t row = 0; row < 8; ++row) {
        if (!(glyph.columns[col] & (1U << row))) continue;
        const int16_t px = x + col * scale;
        const int16_t py = y + row * scale;
        for (uint8_t sy = 0; sy < scale; ++sy) {
          for (uint8_t sx = 0; sx < scale; ++sx) {
            const int16_t targetX = px + sx;
            const int16_t targetY = py + sy;
            if (targetX >= 0 && targetX < DISPLAY_WIDTH && targetY >= 0 && targetY < lineHeight) {
              lineBuffer_[targetY * DISPLAY_WIDTH + targetX] = color;
            }
          }
        }
      }
    }
    x += GLYPH_ADVANCE * scale;
  }
}

void UIRenderer::drawText(int16_t x, int16_t y, const String& text, uint8_t scale,
                          uint16_t color, uint8_t maxGlyphs) {
  drawEncodedText(x, y, encodeText(text, maxGlyphs), scale, color);
}

void UIRenderer::drawEncodedText(int16_t x, int16_t y, const String& encodedText,
                                 uint8_t scale, uint16_t color) {
  if (scale == 0) return;
  for (size_t i = 0; i < encodedText.length(); ++i) {
    drawGlyph(x, y, static_cast<uint8_t>(encodedText[i]), scale, color);
    x += GLYPH_ADVANCE * scale;
  }
}

void UIRenderer::drawSpeakerGlyph(int16_t x, int16_t y) {
  // Skala 1,5x najbliĹĽszego sÄ…siada dla natywnego glyphu gĹ‚oĹ›nika yoRadio.
  const YoRadioGlyph& glyph = yoRadioGlyph(0x13);
  for (uint8_t col = 0; col < 5; ++col) {
    for (uint8_t row = 0; row < 8; ++row) {
      if (!(glyph.columns[col] & (1U << row))) continue;
      const int16_t left = x + (col * 3) / 2;
      const int16_t right = x + ((col + 1) * 3) / 2;
      const int16_t top = y + (row * 3) / 2;
      const int16_t bottom = y + ((row + 1) * 3) / 2;
      display_.fillRect(left, top, right - left, bottom - top, WHITE);
    }
  }
}

void UIRenderer::drawCentered(int16_t y, const String& text, uint8_t scale, uint16_t color,
                              uint8_t maxGlyphs) {
  const String encoded = encodeText(text, maxGlyphs);
  drawEncodedText((DISPLAY_WIDTH - textWidth(encoded, scale)) / 2, y, encoded, scale, color);
}

void UIRenderer::drawGlyph(int16_t x, int16_t y, uint8_t glyphCode, uint8_t scale,
                           uint16_t color) {
  const YoRadioGlyph& glyph = yoRadioGlyph(glyphCode);
  for (uint8_t col = 0; col < 5; ++col) {
    for (uint8_t row = 0; row < 8; ++row) {
      if (glyph.columns[col] & (1U << row)) {
        display_.fillRect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

String UIRenderer::encodeText(const String& source, uint8_t maxGlyphs) const {
  String result;
  bool truncated = false;
  for (size_t i = 0; i < source.length();) {
    uint8_t out = '?';
    const uint8_t first = static_cast<uint8_t>(source[i]);
    if (first < 0x80) { out = first; ++i; }
    else if (i + 1 < source.length()) {
      const uint8_t second = static_cast<uint8_t>(source[i + 1]);
      if (first == 0xC4) {
        if (second == 0x85) out = 0xB8; else if (second == 0x84) out = 0xB7;
        else if (second == 0x87) out = 0xBD; else if (second == 0x86) out = 0xC4;
        else if (second == 0x99) out = 0xD6; else if (second == 0x98) out = 0xD7;
      } else if (first == 0xC5) {
        if (second == 0x82) out = 0xCF; else if (second == 0x81) out = 0xD0;
        else if (second == 0x84) out = 0xC0; else if (second == 0x83) out = 0xC1;
        else if (second == 0x9B) out = 0xCB; else if (second == 0x9A) out = 0xCC;
        else if (second == 0xBA) out = 0xBB; else if (second == 0xB9) out = 0xBC;
        else if (second == 0xBC) out = 0xB9; else if (second == 0xBB) out = 0xBA;
      } else if (first == 0xC3) {
        if (second == 0xB3) out = 0xBE; else if (second == 0x93) out = 0xBF;
      }
      i += 2;
    } else { ++i; }
    if (maxGlyphs != 0 && result.length() >= maxGlyphs) { truncated = true; break; }
    result += static_cast<char>(out);
  }
  if (truncated && maxGlyphs >= 3) result = result.substring(0, maxGlyphs - 3) + "...";
  return result;
}

uint16_t UIRenderer::textWidth(const String& encodedText, uint8_t scale) const {
  return static_cast<uint16_t>(encodedText.length() * GLYPH_ADVANCE * scale);
}

void UIRenderer::invalidateMainCache() {
  lastStation_ = lastArtist_ = lastTitle_ = lastVolume_ = lastRadioName_ = lastClock_ = "\x01";
  lastRssi_ = INT32_MIN;
  lastErrorMode_ = false;
  lastBatteryPercent_ = 255;
  lastBatteryKnown_ = false;
  lastBitrateVisible_ = false;
  lastBottomColor_ = 0;
  activeScroll_ = -1;
  nextScrollCandidate_ = 0;
  for (uint8_t line = 0; line < 3; ++line) scrollLines_[line].text = "\x01";
}
