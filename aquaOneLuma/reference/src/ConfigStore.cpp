#include "ConfigStore.h"

#include <memory>
#include <new>

#include <Preferences.h>
#include <string.h>

#include "ConfigValidator.h"
#include "DebugLog.h"
#include "RuntimeConfig.h"

namespace {
constexpr char NVS_NAMESPACE[] = "yocfg";
constexpr char NVS_PRIMARY_KEY[] = "config";
constexpr char NVS_BACKUP_KEY[] = "config_bak";
constexpr uint32_t CONFIG_MAGIC = 0x594F4346UL;  // "YOCF"
constexpr uint16_t CONFIG_SCHEMA_V1 = 1;

struct __attribute__((packed)) ConfigRadioV1 {
  uint8_t id;
  uint8_t enabled;
  char name[RUNTIME_RADIO_NAME_LEN];
  char host[RUNTIME_RADIO_HOST_LEN];
  uint16_t wsPort;
  char wsPath[RUNTIME_RADIO_PATH_LEN];
};

struct __attribute__((packed)) ConfigPayloadV1 {
  char wifiSsid[RUNTIME_WIFI_SSID_LEN];
  char wifiPass[RUNTIME_WIFI_PASS_LEN];
  uint8_t useStaticIp;
  char staticIp[RUNTIME_IP_LEN];
  char gatewayIp[RUNTIME_IP_LEN];
  char subnetMask[RUNTIME_IP_LEN];
  char dns1Ip[RUNTIME_IP_LEN];
  char dns2Ip[RUNTIME_IP_LEN];
  char timezone[RUNTIME_TIMEZONE_LEN];
  char ntpServer[RUNTIME_NTP_SERVER_LEN];
  ConfigRadioV1 radios[YORADIO_MAX];
  uint32_t volumeScreenTimeoutMs;
  uint32_t volumeRepeatMs;
  uint32_t scrollStartDelayMs;
  uint32_t scrollStepMs;
  uint32_t radioMenuTimeoutMs;
  uint32_t radioConnectingMinMs;
  uint32_t deepSleepTimeoutSec;
  uint32_t stoppedTimeoutSec;
};

struct __attribute__((packed)) ConfigHeader {
  uint32_t magic;
  uint16_t schemaVersion;
  uint16_t payloadLength;
  uint32_t crc32;
};

struct __attribute__((packed)) ConfigRecordV1 {
  ConfigHeader header;
  ConfigPayloadV1 payload;
};

static_assert(sizeof(ConfigRadioV1) == 135, "Unexpected ConfigRadioV1 layout");
static_assert(sizeof(ConfigPayloadV1) == 1555, "Unexpected ConfigPayloadV1 layout");
static_assert(sizeof(ConfigHeader) == 12, "Unexpected ConfigHeader layout");

enum class SnapshotState : uint8_t { MISSING, RECORD, RAW };

struct SnapshotBlob {
  SnapshotState state{SnapshotState::MISSING};
  ConfigRecordV1 record{};
  std::unique_ptr<uint8_t[]> rawBytes{};
  size_t rawLength{0};
};

// ConfigStore działa wyłącznie w zadaniu pętli Arduino; duży rekord v1 i
// zdekodowany RuntimeConfig pozostają poza stosem tego zadania.
struct ConfigStoreWorkspace {
  ConfigRecordV1 record{};
  RuntimeConfig candidate{};
  SnapshotBlob rollbackPrimary{};
  SnapshotBlob rollbackBackup{};
};
ConfigStoreWorkspace workspace;

void clearSnapshotBlob(SnapshotBlob& blob) {
  blob.state = SnapshotState::MISSING;
  blob.record = ConfigRecordV1{};
  blob.rawBytes.reset();
  blob.rawLength = 0;
}
enum class RecordState : uint8_t {
  MISSING,
  LENGTH,
  READ,
  MAGIC,
  SCHEMA,
  PAYLOAD_LENGTH,
  CRC,
  VALIDATION,
  VALID,
};

const char* recordStateName(RecordState state) {
  switch (state) {
    case RecordState::MISSING: return "missing";
    case RecordState::LENGTH: return "invalid reason=length";
    case RecordState::READ: return "invalid reason=read";
    case RecordState::MAGIC: return "invalid reason=magic";
    case RecordState::SCHEMA: return "invalid reason=schema";
    case RecordState::PAYLOAD_LENGTH: return "invalid reason=payload_length";
    case RecordState::CRC: return "invalid reason=crc";
    case RecordState::VALIDATION: return "invalid reason=validation";
    case RecordState::VALID: return "valid";
  }
  return "invalid reason=unknown";
}

uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & static_cast<uint32_t>(-(crc & 1U)));
    }
  }
  return ~crc;
}

void toPayload(const RuntimeConfig& config, ConfigPayloadV1& payload) {
  memset(&payload, 0, sizeof(payload));
  memcpy(payload.wifiSsid, config.wifiSsid, sizeof(payload.wifiSsid));
  memcpy(payload.wifiPass, config.wifiPass, sizeof(payload.wifiPass));
  payload.useStaticIp = config.useStaticIp ? 1 : 0;
  memcpy(payload.staticIp, config.staticIp, sizeof(payload.staticIp));
  memcpy(payload.gatewayIp, config.gatewayIp, sizeof(payload.gatewayIp));
  memcpy(payload.subnetMask, config.subnetMask, sizeof(payload.subnetMask));
  memcpy(payload.dns1Ip, config.dns1Ip, sizeof(payload.dns1Ip));
  memcpy(payload.dns2Ip, config.dns2Ip, sizeof(payload.dns2Ip));
  memcpy(payload.timezone, config.timezone, sizeof(payload.timezone));
  memcpy(payload.ntpServer, config.ntpServer, sizeof(payload.ntpServer));
  for (uint8_t i = 0; i < YORADIO_MAX; ++i) {
    const RuntimeRadioConfig& source = config.radios[i];
    ConfigRadioV1& target = payload.radios[i];
    target.id = source.id;
    target.enabled = source.enabled ? 1 : 0;
    target.wsPort = source.wsPort;
    memcpy(target.name, source.name, sizeof(target.name));
    memcpy(target.host, source.host, sizeof(target.host));
    memcpy(target.wsPath, source.wsPath, sizeof(target.wsPath));
  }
  payload.volumeScreenTimeoutMs = config.volumeScreenTimeoutMs;
  payload.volumeRepeatMs = config.volumeRepeatMs;
  payload.scrollStartDelayMs = config.scrollStartDelayMs;
  payload.scrollStepMs = config.scrollStepMs;
  payload.radioMenuTimeoutMs = config.radioMenuTimeoutMs;
  payload.radioConnectingMinMs = config.radioConnectingMinMs;
  payload.deepSleepTimeoutSec = config.deepSleepTimeoutSec;
  payload.stoppedTimeoutSec = config.stoppedTimeoutSec;
}

void fromPayload(const ConfigPayloadV1& payload, RuntimeConfig& config) {
  config = RuntimeConfig{};
  memcpy(config.wifiSsid, payload.wifiSsid, sizeof(config.wifiSsid));
  memcpy(config.wifiPass, payload.wifiPass, sizeof(config.wifiPass));
  config.useStaticIp = payload.useStaticIp != 0;
  memcpy(config.staticIp, payload.staticIp, sizeof(config.staticIp));
  memcpy(config.gatewayIp, payload.gatewayIp, sizeof(config.gatewayIp));
  memcpy(config.subnetMask, payload.subnetMask, sizeof(config.subnetMask));
  memcpy(config.dns1Ip, payload.dns1Ip, sizeof(config.dns1Ip));
  memcpy(config.dns2Ip, payload.dns2Ip, sizeof(config.dns2Ip));
  memcpy(config.timezone, payload.timezone, sizeof(config.timezone));
  memcpy(config.ntpServer, payload.ntpServer, sizeof(config.ntpServer));
  for (uint8_t i = 0; i < YORADIO_MAX; ++i) {
    RuntimeRadioConfig& target = config.radios[i];
    const ConfigRadioV1& source = payload.radios[i];
    target.id = source.id;
    target.enabled = source.enabled != 0;
    target.wsPort = source.wsPort;
    memcpy(target.name, source.name, sizeof(target.name));
    memcpy(target.host, source.host, sizeof(target.host));
    memcpy(target.wsPath, source.wsPath, sizeof(target.wsPath));
  }
  config.volumeScreenTimeoutMs = payload.volumeScreenTimeoutMs;
  config.volumeRepeatMs = payload.volumeRepeatMs;
  config.scrollStartDelayMs = payload.scrollStartDelayMs;
  config.scrollStepMs = payload.scrollStepMs;
  config.radioMenuTimeoutMs = payload.radioMenuTimeoutMs;
  config.radioConnectingMinMs = payload.radioConnectingMinMs;
  config.deepSleepTimeoutSec = payload.deepSleepTimeoutSec;
  config.stoppedTimeoutSec = payload.stoppedTimeoutSec;
}

RecordState validateRecord(const ConfigRecordV1& record, RuntimeConfig& candidate) {
  if (record.header.magic != CONFIG_MAGIC) return RecordState::MAGIC;
  if (record.header.schemaVersion != CONFIG_SCHEMA_V1) return RecordState::SCHEMA;
  if (record.header.payloadLength != sizeof(ConfigPayloadV1)) return RecordState::PAYLOAD_LENGTH;
  if (crc32(reinterpret_cast<const uint8_t*>(&record.payload), sizeof(record.payload)) !=
      record.header.crc32) return RecordState::CRC;

  fromPayload(record.payload, candidate);
  ConfigValidationError error;
  uint8_t radioIndex;
  if (!ConfigValidator::validate(candidate, error, radioIndex)) return RecordState::VALIDATION;
  return RecordState::VALID;
}

RecordState readRecord(Preferences& preferences, const char* key, ConfigRecordV1& record,
                       RuntimeConfig& candidate) {
  const size_t length = preferences.getBytesLength(key);
  if (length == 0) return RecordState::MISSING;
  if (length != sizeof(record)) return RecordState::LENGTH;
  if (preferences.getBytes(key, &record, sizeof(record)) != sizeof(record)) {
    return RecordState::READ;
  }
  return validateRecord(record, candidate);
}

bool writeRecord(Preferences& preferences, const char* key, const ConfigRecordV1& record,
                 size_t& bytesWritten) {
  bytesWritten = preferences.putBytes(key, &record, sizeof(record));
  return bytesWritten == sizeof(record);
}

void makeRecord(const RuntimeConfig& config, ConfigRecordV1& record) {
  record = ConfigRecordV1{};
  record.header.magic = CONFIG_MAGIC;
  record.header.schemaVersion = CONFIG_SCHEMA_V1;
  record.header.payloadLength = sizeof(ConfigPayloadV1);
  toPayload(config, record.payload);
  record.header.crc32 = crc32(reinterpret_cast<const uint8_t*>(&record.payload),
                             sizeof(record.payload));
}
}  // namespace

bool ConfigStore::capturePrimaryForRollback() {
  clearRollbackSnapshot();
  Preferences preferences;
  // Otwarcie do odczytu/zapisu działa także przy pierwszym zapisie bez istniejącego namespace.
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;

  const auto captureKey = [&preferences](const char* key, SnapshotBlob& blob) {
    clearSnapshotBlob(blob);
    if (!preferences.isKey(key)) return true;

    // Preferences potrafi odtworzyć bajt w bajt tylko niepusty blob; obecny klucz,
    // którego nie da się tak zapisać, bezpiecznie przerywa snapshot.
    if (preferences.getType(key) != PT_BLOB) return false;
    const size_t length = preferences.getBytesLength(key);
    if (length == 0) return false;

    if (length == sizeof(blob.record)) {
      if (preferences.getBytes(key, &blob.record, sizeof(blob.record)) != sizeof(blob.record)) {
        return false;
      }
      blob.state = SnapshotState::RECORD;
      return true;
    }

    blob.rawBytes.reset(new (std::nothrow) uint8_t[length]);
    if (!blob.rawBytes) return false;
    if (preferences.getBytes(key, blob.rawBytes.get(), length) != length) return false;
    blob.rawLength = length;
    blob.state = SnapshotState::RAW;
    return true;
  };

  const bool captured = captureKey(NVS_PRIMARY_KEY, workspace.rollbackPrimary) &&
                        captureKey(NVS_BACKUP_KEY, workspace.rollbackBackup);
  preferences.end();
  if (!captured) clearRollbackSnapshot();
  return captured;
}

bool ConfigStore::restorePrimaryFromRollback() {
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) return false;

  const auto restoreKey = [&preferences](const char* key, const SnapshotBlob& blob) {
    if (blob.state == SnapshotState::MISSING) {
      if (!preferences.isKey(key)) return true;
      return preferences.remove(key) || !preferences.isKey(key);
    }
    if (blob.state == SnapshotState::RECORD) {
      return preferences.putBytes(key, &blob.record, sizeof(blob.record)) == sizeof(blob.record);
    }
    if (!blob.rawBytes || blob.rawLength == 0) return false;
    return preferences.putBytes(key, blob.rawBytes.get(), blob.rawLength) == blob.rawLength;
  };

  // ConfigStore::save() mógł zmienić config_bak, dlatego odtwarzamy oba rekordy.
  const bool backupRestored = restoreKey(NVS_BACKUP_KEY, workspace.rollbackBackup);
  const bool primaryRestored = restoreKey(NVS_PRIMARY_KEY, workspace.rollbackPrimary);
  preferences.end();
  return backupRestored && primaryRestored;
}

void ConfigStore::clearRollbackSnapshot() {
  clearSnapshotBlob(workspace.rollbackPrimary);
  clearSnapshotBlob(workspace.rollbackBackup);
}bool ConfigStore::validate(const RuntimeConfig& config) const {
  ConfigValidationError error;
  uint8_t radioIndex;
  return ConfigValidator::validate(config, error, radioIndex);
}

bool ConfigStore::load(RuntimeConfig& config) {
  hasValidConfig_ = false;
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, true)) {
    DEBUG_LOGLN("[CFG] primary: missing");
    DEBUG_LOGLN("[CFG] backup: missing");
    DEBUG_LOGLN("[CFG] no valid config -> config mode");
    return false;
  }

  ConfigRecordV1& record = workspace.record;
  RuntimeConfig& candidate = workspace.candidate;
  const RecordState primaryState =
      readRecord(preferences, NVS_PRIMARY_KEY, record, candidate);
  DEBUG_LOGF("[CFG] primary: %s\n", recordStateName(primaryState));
  if (primaryState == RecordState::VALID) {
    preferences.end();
    config = candidate;
    hasValidConfig_ = true;
    DEBUG_LOGLN("[CFG] loaded source=primary");
    return true;
  }

  const RecordState backupState =
      readRecord(preferences, NVS_BACKUP_KEY, record, candidate);
  preferences.end();
  DEBUG_LOGF("[CFG] backup: %s\n", recordStateName(backupState));
  if (backupState == RecordState::VALID) {
    config = candidate;
    hasValidConfig_ = true;
    DEBUG_LOGLN("[CFG] loaded source=backup");
    return true;
  }

  DEBUG_LOGLN("[CFG] no valid config -> config mode");
  return false;
}

bool ConfigStore::save(const RuntimeConfig& config) {
  if (!validate(config)) {
    DEBUG_LOGLN("[CFG] save rejected: candidate invalid");
    return false;
  }

  DEBUG_LOGLN("[CFG] save begin");
  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    DEBUG_LOGLN("[CFG] save failed: namespace open");
    return false;
  }

  ConfigRecordV1& record = workspace.record;
  RuntimeConfig& candidate = workspace.candidate;
  const RecordState primaryState =
      readRecord(preferences, NVS_PRIMARY_KEY, record, candidate);
  DEBUG_LOGF("[CFG] old primary: %s\n", recordStateName(primaryState));

  if (primaryState == RecordState::VALID) {
    size_t backupBytes = 0;
    if (!writeRecord(preferences, NVS_BACKUP_KEY, record, backupBytes)) {
      DEBUG_LOGLN("[CFG] backup write: fail");
      preferences.end();
      return false;
    }
    const RecordState backupState =
        readRecord(preferences, NVS_BACKUP_KEY, record, candidate);
    if (backupState != RecordState::VALID) {
      DEBUG_LOGF("[CFG] backup write: fail reason=%s\n", recordStateName(backupState));
      preferences.end();
      return false;
    }
    DEBUG_LOGLN("[CFG] backup write: ok");
  } else {
    DEBUG_LOGLN("[CFG] backup write: skipped");
  }

  makeRecord(config, record);
  size_t primaryBytes = 0;
  if (!writeRecord(preferences, NVS_PRIMARY_KEY, record, primaryBytes)) {
    DEBUG_LOGF("[CFG] primary write: bytes=%u fail\n", static_cast<unsigned>(primaryBytes));
    preferences.end();
    return false;
  }
  DEBUG_LOGF("[CFG] primary write: bytes=%u\n", static_cast<unsigned>(primaryBytes));

  const RecordState verifyState =
      readRecord(preferences, NVS_PRIMARY_KEY, record, candidate);
  preferences.end();
  if (verifyState != RecordState::VALID) {
    DEBUG_LOGF("[CFG] primary verify: fail reason=%s\n", recordStateName(verifyState));
    return false;
  }

  hasValidConfig_ = true;
  DEBUG_LOGLN("[CFG] primary verify: ok");
  DEBUG_LOGLN("[CFG] save success");
  return true;
}