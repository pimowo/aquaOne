#include "AquaCore/System/SystemService.h"

namespace AquaCore {
namespace {

void terminateIdentityFields(DeviceIdentity& identity) {
    identity.deviceType[
        DeviceIdentity::DEVICE_TYPE_CAPACITY - 1U
    ] = '\0';
    identity.deviceName[
        DeviceIdentity::DEVICE_NAME_CAPACITY - 1U
    ] = '\0';
    identity.firmwareVersion[
        DeviceIdentity::FIRMWARE_VERSION_CAPACITY - 1U
    ] = '\0';
    identity.hardwareVariant[
        DeviceIdentity::HARDWARE_VARIANT_CAPACITY - 1U
    ] = '\0';
}

} // namespace

SystemService::SystemService(SystemBackend& backend)
    : backend_(&backend),
      identity_ {},
      restartReason_(RestartReason::Unknown),
      state_(SystemState::NotStarted) {
}

bool SystemService::begin(const DeviceIdentity& identity) {
    identity_ = identity;
    terminateIdentityFields(identity_);
    restartReason_ = backend_->restartReason();
    state_ = SystemState::Ready;
    return true;
}

bool SystemService::isReady() const {
    return state_ == SystemState::Ready;
}

uint32_t SystemService::uptimeMs() const {
    return backend_->uptimeMs();
}

RestartReason SystemService::restartReason() const {
    return restartReason_;
}

const DeviceIdentity& SystemService::deviceIdentity() const {
    return identity_;
}

const char* SystemService::aquaCoreVersion() const {
    return AQUA_CORE_VERSION;
}

SystemStatus SystemService::status() const {
    SystemStatus value {};
    value.state = state_;
    value.restartReason = restartReason_;
    value.uptimeMs = uptimeMs();
    return value;
}

} // namespace AquaCore
