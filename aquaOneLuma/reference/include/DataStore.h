#pragma once
#include <Arduino.h>

enum class DataUpdateResult : uint8_t {
  INVALID_JSON,
  NO_PAYLOAD,
  NO_RECOGNIZED_DATA,
  ACCEPTED,
};

class DataStore {
public:
  DataStore() = default;
  void begin();
  void beginRadio(uint8_t radioId, const char* radioName);
  void clearRadioData();
  void clearSessionData();
  void clearMediaData();

  // Rozróżnia poprawność JSON od zaakceptowanych danych payload yoRadio.
  DataUpdateResult updateFromJson(const uint8_t* payload, size_t length);

  const String& station() const { return station_; }
  const String& artist() const { return artist_; }
  const String& title() const { return title_; }
  const String& volume() const { return volume_; }
  const String& bitrate() const { return bitrate_; }
  const String& format() const { return format_; }
  const String& playerState() const { return playerState_; }
  int rssi() const { return rssi_; }
  int heap() const { return heap_; }
  int bass() const { return bass_; }
  int middle() const { return middle_; }
  int trebble() const { return trebble_; }
  int balance() const { return balance_; }
  uint8_t radioId() const { return radioId_; }
  const String& radioName() const { return radioName_; }

private:
  void applyMeta(const char* meta);
  void setRawValue(String& target, const char* value);

  String station_;
  String artist_;
  String title_;
  String volume_;
  String bitrate_;
  String format_;
  String playerState_;
  int rssi_{0};
  int heap_{0};
  int bass_{0};
  int middle_{0};
  int trebble_{0};
  int balance_{0};
  uint8_t radioId_{0};
  String radioName_;
};
