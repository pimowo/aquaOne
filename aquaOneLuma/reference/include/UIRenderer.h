#pragma once

#include <Arduino.h>

class ClockService;
class BatteryMonitor;
class DataStore;
class DisplayDriver;
class RadioManager;
class PowerStatusMonitor;
class WiFiMgr;
class WSClient;
struct RuntimeConfig;

enum class RssiZone : uint8_t { UNKNOWN, GREEN, YELLOW, RED };

class UIRenderer {
public:
  UIRenderer(DisplayDriver& display, WiFiMgr& wifi, WSClient& ws, DataStore& dataStore,
             RadioManager& radios, ClockService& clock, BatteryMonitor& battery,
             PowerStatusMonitor& powerStatus,
             const RuntimeConfig& config);
  void begin();
  void loop();
  void showVolumeScreen();
  bool isVolumeScreenActive() const;
  bool openRadioSelect();
  bool isRadioSelectActive() const;
  void moveRadioSelection(int8_t direction);
  void confirmRadioSelection();
  int8_t consumeRadioSelection();

private:
  enum class Screen : uint8_t { WIFI_CONNECTING, RADIO_CONNECTING, MAIN, VOLUME, RADIO_SELECT, WS_ERROR };
  void renderStartup(bool force);
  void renderMain(bool force);
  void renderVolumeScreen(bool force);
  void renderRadioSelect(bool force);
  void drawRadioSelectRow(int8_t index);
  void drawRadioSelectIndicators(bool force);
  void drawBitrateBox(bool force);
  void drawRssi(bool force, bool error);
  const char* errorLabel() const;
  void drawClock(bool force);
  void drawBattery();
  void drawPowerIndicators(bool force);
  void updateScroll();
  void renderMediaLine(uint8_t lineIndex);
  void setScrollText(uint8_t lineIndex, const String& text);
  void resetScroll(uint8_t lineIndex);
  bool updateActiveScroll();
  bool lineNeedsScroll(uint8_t lineIndex) const;
  void drawEncodedTextToLine(const String& text, int16_t x, int16_t y, uint8_t scale,
                             uint16_t color, uint8_t lineHeight);
  void drawText(int16_t x, int16_t y, const String& text, uint8_t scale, uint16_t color,
                uint8_t maxGlyphs = 0);
  void drawEncodedText(int16_t x, int16_t y, const String& encodedText, uint8_t scale,
                       uint16_t color);
  void drawSpeakerGlyph(int16_t x, int16_t y);
  void drawCentered(int16_t y, const String& text, uint8_t scale, uint16_t color,
                    uint8_t maxGlyphs = 0);
  void drawGlyph(int16_t x, int16_t y, uint8_t glyphCode, uint8_t scale, uint16_t color);
  String encodeText(const String& source, uint8_t maxGlyphs) const;
  uint16_t textWidth(const String& encodedText, uint8_t scale) const;
  void invalidateMainCache();

  struct ScrollLine {
    String text;
    uint16_t textWidth{0};
    int16_t offset{0};
    uint32_t lastStepAt{0};
    bool started{false};
  };

  DisplayDriver& display_;
  WiFiMgr& wifi_;
  WSClient& ws_;
  DataStore& dataStore_;
  RadioManager& radios_;
  ClockService& clock_;
  BatteryMonitor& battery_;
  PowerStatusMonitor& powerStatus_;
  const RuntimeConfig& config_;
  Screen screen_{Screen::WIFI_CONNECTING};
  uint32_t lastAnimationAt_{0};
  uint32_t radioConnectingSince_{0};
  uint32_t lastMainCheckAt_{0};
  uint32_t volumeScreenUntil_{0};
  uint32_t radioSelectUntil_{0};
  uint32_t lastClockBlinkAt_{0};
  uint8_t dots_{1};
  String lastStation_, lastArtist_, lastTitle_, lastVolume_, lastRadioName_, lastClock_;
  enum class RssiSource : uint8_t { RADIO, PILOT };
  RssiSource rssiSource_{RssiSource::RADIO};
  int lastRssi_{INT32_MIN};
  uint32_t lastRssiDrawAt_{0};
  RssiZone radioRssiZone_{RssiZone::UNKNOWN};
  int lastPilotRssi_{INT32_MIN};
  uint32_t lastPilotRssiReadAt_{0};
  RssiZone pilotRssiZone_{RssiZone::UNKNOWN};
  int renderedRssi_{INT32_MIN};
  bool rssiUnitDrawn_{false};
  uint16_t lastRssiUnitColor_{0};
  bool clockColonVisible_{true};
  bool lastErrorMode_{false};
  bool volumeScreenDrawn_{false};
  String lastVolumeScreenValue_;
  int8_t radioSelectIndex_{-1};
  bool radioSelectUpVisible_{false};
  bool radioSelectDownVisible_{false};
  int8_t confirmedRadioIndex_{-1};
  bool reachedMain_{false};
  uint8_t lastBatteryPercent_{255};
  bool lastBatteryKnown_{false};
  enum class PowerIconState : uint8_t { NONE, PLUG, CHARGING };
  PowerIconState powerIconState_{PowerIconState::NONE};
  String lastBitrate_, lastFormat_;
  bool lastBitrateVisible_{false};
  uint16_t lastBottomColor_{0};
  ScrollLine scrollLines_[3];
  int8_t activeScroll_{-1};
  uint8_t nextScrollCandidate_{0};
  uint16_t lineBuffer_[128 * 18]{};
};
