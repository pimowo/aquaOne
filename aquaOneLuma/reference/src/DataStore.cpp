#include "DataStore.h"
#include <Arduino.h>
#include <ArduinoJson.h>

#include "DebugLog.h"
namespace {
void logUnsupportedId(const char* id) {
  DEBUG_LOGF("[DATA] unsupported id: %s\n", (id && id[0] != '\0') ? id : "<missing>");
}

void logUnsupportedValue(const char* id) {
  DEBUG_LOGF("[DATA] unsupported value for id: %s\n", id);
}
}

void DataStore::begin() {
  DEBUG_LOGLN("[DATA] DataStore ready");
}

void DataStore::beginRadio(uint8_t radioId, const char* radioName) {
  radioId_ = radioId;
  radioName_ = radioName ? radioName : "";
  clearRadioData();
  DEBUG_LOGF("[DATA][%lu] active radio %u: %s\n", millis(), radioId_, radioName_.c_str());
}

void DataStore::clearRadioData() {
  clearSessionData();
}

void DataStore::clearSessionData() {
  clearMediaData();
  volume_ = "";
  bitrate_ = "";
  format_ = "";
  playerState_ = "";
  rssi_ = 0;
  heap_ = 0;
  bass_ = 0;
  middle_ = 0;
  trebble_ = 0;
  balance_ = 0;
}

void DataStore::clearMediaData() {
  station_ = "";
  artist_ = "";
  title_ = "";
}

DataUpdateResult DataStore::updateFromJson(const uint8_t* payload, size_t length) {
  StaticJsonDocument<1024> doc;
  const DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    return DataUpdateResult::INVALID_JSON;
  }

  JsonArrayConst entries = doc["payload"].as<JsonArrayConst>();
  if (entries.isNull()) {
    DEBUG_LOGLN("[DATA] unsupported JSON structure: payload[] missing");
    return DataUpdateResult::NO_PAYLOAD;
  }

  bool recognizedDataAccepted = false;
  for (JsonObjectConst entry : entries) {
    const char* id = entry["id"] | "";
    JsonVariantConst value = entry["value"];

    if (strcmp(id, "nameset") == 0) {
      if (value.is<const char*>()) {
        setRawValue(station_, value.as<const char*>());
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "meta") == 0) {
      if (value.is<const char*>()) {
        applyMeta(value.as<const char*>());
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "volume") == 0) {
      if (!value.isNull()) {
        volume_ = value.as<String>();
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "bitrate") == 0) {
      if (value.is<int>()) {
        bitrate_ = value.as<String>();
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "fmt") == 0) {
      if (value.is<const char*>()) {
        const char* format = value.as<const char*>();
        format_ = strcmp(format, "bitrate") == 0 ? "kbs" : format;
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "playerwrap") == 0) {
      if (value.is<const char*>()) {
        setRawValue(playerState_, value.as<const char*>());
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "rssi") == 0) {
      if (value.is<int>()) {
        rssi_ = value.as<int>();
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "heap") == 0) {
      if (value.is<int>()) {
        heap_ = value.as<int>();
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "bass") == 0) {
      if (value.is<int>()) {
        bass_ = value.as<int>();
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "middle") == 0) {
      if (value.is<int>()) {
        middle_ = value.as<int>();
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "trebble") == 0) {
      if (value.is<int>()) {
        trebble_ = value.as<int>();
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else if (strcmp(id, "balance") == 0) {
      if (value.is<int>()) {
        balance_ = value.as<int>();
        recognizedDataAccepted = true;
      } else {
        logUnsupportedValue(id);
      }
    } else {
      logUnsupportedId(id);
    }
  }
  return recognizedDataAccepted ? DataUpdateResult::ACCEPTED
                               : DataUpdateResult::NO_RECOGNIZED_DATA;
}

void DataStore::applyMeta(const char* meta) {
  // Zachowana zgodność z OLD_WEBSOCKET, w tym z aliasami stanów.
  if (strstr(meta, "[connecting]") || strstr(meta, "[łącze]") ||
      strstr(meta, "[loading]") || strstr(meta, "[buffering]")) {
    artist_ = "";
    title_ = "Łączę";
    playerState_ = "connecting";
  } else if (strstr(meta, "[stopped]") || strstr(meta, "[zatrzymany]") ||
             strstr(meta, "[stop]")) {
    artist_ = "";
    title_ = "Zatrzymany";
    playerState_ = "stop";
  } else if (strstr(meta, "[paused]") || strstr(meta, "[pauza]")) {
    artist_ = "";
    title_ = "Pauza";
    playerState_ = "paused";
  } else if (strstr(meta, "[error]") || strstr(meta, "[błąd]")) {
    artist_ = "";
    title_ = "Błąd";
    playerState_ = "error";
  } else {
    const char* separator = strstr(meta, " - ");
    artist_ = "";
    if (separator) {
      artist_ = String(meta).substring(0, separator - meta);
      title_ = separator + 3;
    } else {
      const size_t length = strlen(meta);
      if (length >= 2 && meta[0] == '[' && meta[length - 1] == ']') {
        title_ = String(meta).substring(1, length - 1);
      } else {
        title_ = meta;
      }
    }
  }
}

void DataStore::setRawValue(String& target, const char* value) {
  target = value ? value : "";
}
