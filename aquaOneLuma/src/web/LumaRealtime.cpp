#include "LumaRealtime.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace LumaSense {
namespace Web {
namespace {

bool sameVisibleFloat(float left, float right) {
    if (!std::isfinite(left)) left = 0.0f;
    if (!std::isfinite(right)) right = 0.0f;
    char a[24] {};
    char b[24] {};
    const int al = std::snprintf(a, sizeof(a), "%.2f", static_cast<double>(left));
    const int bl = std::snprintf(b, sizeof(b), "%.2f", static_cast<double>(right));
    return al > 0 && bl > 0 && static_cast<size_t>(al) < sizeof(a) &&
        static_cast<size_t>(bl) < sizeof(b) && std::strcmp(a, b) == 0;
}

} // namespace

LumaRealtime::LumaRealtime(
    LumaWebApplication& app,
    AquaCore::Web::PublishedSnapshot<LumaPublishedStatus>& status,
    AquaCore::Web::EspIdfWebTransport& transport,
    AquaCore::Web::NativeWebService& service)
    : LumaRealtime(app, status, transport, service, LifecyclePort()) {}

LumaRealtime::LumaRealtime(
    LumaWebApplication& app,
    AquaCore::Web::PublishedSnapshot<LumaPublishedStatus>& status,
    AquaCore::Web::EspIdfWebTransport& transport,
    AquaCore::Web::NativeWebService& service,
    LifecyclePort lifecycle)
    : app_(app), status_(status), transport_(transport), service_(service),
      lifecycle_(lifecycle) {}

LumaRealtime::~LumaRealtime() {
    if (cohort_ != nullptr) cohort_->~Cohort();
    if (sequencer_ != nullptr) sequencer_->~RealtimeStreamSequencer();
}

bool LumaRealtime::start(AquaCore::Identity::RuntimeIdentity runtime) {
    if (sequencer_ != nullptr || !runtime.isValid()) return false;
    runtime_ = runtime;
    sequencer_ = new (&sequencerStorage_)
        AquaCore::Web::RealtimeStreamSequencer(runtime);
    resource_ = AquaCore::Web::RealtimeResyncResourceBinding(
        this, invalidateResource, buildResource, publishResource);
    cohort_ = new (&cohortStorage_) Cohort(runtime, *sequencer_, &resource_, 1U,
        publishStream, this, notify, this, requestRecovery, this);
    const auto result = cohort_->publishBaseline();
    if (result != AquaCore::Web::RealtimeCohortPublicationResult::Success)
        return false;
    rememberCoherent();
    active_ = true;
    return true;
}

bool LumaRealtime::invalidateResource(void* context) {
    return static_cast<LumaRealtime*>(context)->status_.invalidate();
}

bool LumaRealtime::buildResource(void* context,
    AquaCore::Identity::RuntimeIdentity runtime,
    AquaCore::Web::RealtimeStreamPosition position) {
    LumaRealtime& self = *static_cast<LumaRealtime*>(context);
    self.pending_ = LumaPublishedStatus();
    if (!runtime.isValid() || !position.isValid() ||
        !self.app_.buildStatus(self.pending_.value)) return false;
    self.pending_.realtimeAvailable = true;
    self.pending_.runtime = runtime;
    self.pending_.position = position;
    return true;
}

bool LumaRealtime::publishResource(void* context) {
    LumaRealtime& self = *static_cast<LumaRealtime*>(context);
    return self.status_.publish(self.pending_);
}

bool LumaRealtime::publishStream(void* context,
    const AquaCore::Web::RealtimeStreamStartState& state) {
    return static_cast<LumaRealtime*>(context)->transport_
        .publishRealtimeStreamStartState(state);
}

AquaCore::Web::RealtimePublicationResult LumaRealtime::notify(void* context,
    const AquaCore::Web::RealtimeNotificationMetadata& metadata) {
    LumaRealtime& self = *static_cast<LumaRealtime*>(context);
    uint8_t frame[NOTIFICATION_MAX_LENGTH + 1U] {};
    size_t length = 0U;
    if (!encodeNotification(metadata, frame, sizeof(frame), length))
        return AquaCore::Web::RealtimePublicationResult::InvalidMetadata;
    return self.transport_.publishRealtime(metadata,
        AquaCore::Web::RealtimeFrameType::Text, frame, length);
}

void LumaRealtime::requestRecovery(void* context) {
    static_cast<LumaRealtime*>(context)->transport_.requestRealtimeRecovery();
}

bool LumaRealtime::encodeNotification(
    const AquaCore::Web::RealtimeNotificationMetadata& metadata,
    uint8_t* output, size_t capacity, size_t& length) {
    length = 0U;
    if (!metadata.isValid() || output == nullptr) return false;
    char runtime[AquaCore::Identity::RuntimeIdentity::TEXT_CAPACITY] {};
    if (!metadata.runtimeIdentity().format(runtime, sizeof(runtime))) return false;
    char frame[NOTIFICATION_MAX_LENGTH + 1U] {};
    const int written = std::snprintf(frame, sizeof(frame),
        "{\"type\":\"notification\",\"runtime_id\":\"%s\",\"sequence\":\"%llu\",\"kind\":\"luma_status_changed\"}",
        runtime, static_cast<unsigned long long>(metadata.sequence().value()));
    if (written <= 0 || static_cast<size_t>(written) > NOTIFICATION_MAX_LENGTH ||
        capacity <= static_cast<size_t>(written)) return false;
    std::memcpy(output, frame, static_cast<size_t>(written) + 1U);
    length = static_cast<size_t>(written);
    return true;
}

bool LumaRealtime::sameSemanticStatus(const LumaStatusProjection& a,
                                       const LumaStatusProjection& b) {
    if (a.mode != b.mode || a.activeProfile != b.activeProfile ||
        std::strncmp(a.activeProfileName, b.activeProfileName,
                     sizeof(a.activeProfileName)) != 0 ||
        a.dayState != b.dayState || a.timeValid != b.timeValid ||
        a.wifiState != b.wifiState || a.wifiConnected != b.wifiConnected ||
        std::memcmp(a.wifiIp.octets, b.wifiIp.octets,
                    sizeof(a.wifiIp.octets)) != 0 ||
        a.overallHealth != b.overallHealth ||
        !sameVisibleFloat(a.globalPowerLimit, b.globalPowerLimit)) return false;
    for (size_t i = 0U; i < CHANNEL_COUNT; ++i) {
        if (!sameVisibleFloat(a.requestedLevels[i], b.requestedLevels[i]) ||
            !sameVisibleFloat(a.finalLevels[i], b.finalLevels[i])) return false;
    }
    return true;
}

void LumaRealtime::rememberCoherent() {
    if (cohort_ != nullptr && cohort_->isCoherentAt(runtime_,
            sequencer_->currentPosition())) {
        lastPublished_ = pending_.value;
        havePublished_ = true;
    }
}

bool LumaRealtime::recoveryRequired() const {
    return lifecycle_.recoveryRequired != nullptr ?
        lifecycle_.recoveryRequired(lifecycle_.context) :
        transport_.isRealtimeRecoveryRequired();
}

AquaCore::Web::RealtimeRecoveryServiceResult LumaRealtime::serviceClientClosures() {
    return lifecycle_.serviceRecovery != nullptr ?
        lifecycle_.serviceRecovery(lifecycle_.context) :
        transport_.serviceRealtimeRecovery();
}

bool LumaRealtime::tryStop() {
    return lifecycle_.tryStop != nullptr ? lifecycle_.tryStop(lifecycle_.context) :
        transport_.tryStop();
}

bool LumaRealtime::begin(const AquaCore::Web::WebConfig& config) {
    return lifecycle_.begin != nullptr ? lifecycle_.begin(lifecycle_.context, config) :
        service_.begin(config);
}

bool LumaRealtime::isRunning() const {
    return lifecycle_.isRunning != nullptr ? lifecycle_.isRunning(lifecycle_.context) :
        service_.isRunning();
}

bool LumaRealtime::tryClear() {
    return lifecycle_.tryClear != nullptr ?
        lifecycle_.tryClear(lifecycle_.context, runtime_,
                            sequencer_->currentPosition()) :
        transport_.tryClearRealtimeRecovery(runtime_, sequencer_->currentPosition());
}

void LumaRealtime::advanceRecycle(uint32_t nowMs,
                                   const AquaCore::Web::WebConfig& config) {
    if (recyclePhase_ == RecyclePhase::None ||
        nowMs - lastRecycleAttemptMs_ < RECYCLE_RETRY_MS) return;
    lastRecycleAttemptMs_ = nowMs;
    if (recyclePhase_ == RecyclePhase::StopPending) {
        if (!tryStop()) return;
        recyclePhase_ = RecyclePhase::RepublishPending;
    }
    if (recyclePhase_ == RecyclePhase::RepublishPending) {
        if (cohort_->republishCurrentCohort() !=
            AquaCore::Web::RealtimeCohortPublicationResult::Success) return;
        rememberCoherent();
        recyclePhase_ = RecyclePhase::RestartPending;
    }
    if (recyclePhase_ == RecyclePhase::RestartPending) {
        if (!begin(config) || !isRunning()) return;
        recyclePhase_ = RecyclePhase::None;
        retryNeeded_ = false;
    }
}

void LumaRealtime::serviceRecovery(uint32_t nowMs,
                                    const AquaCore::Web::WebConfig& config) {
    if (nowMs - lastRecoveryMs_ < RECOVERY_INTERVAL_MS) return;
    lastRecoveryMs_ = nowMs;
    if (recyclePhase_ != RecyclePhase::None) {
        advanceRecycle(nowMs, config);
        return;
    }

    // Core also owns per-client Closing slots without a global sticky flag.
    const auto result = serviceClientClosures();
    if (result == AquaCore::Web::RealtimeRecoveryServiceResult::RetryNeeded) {
        if (!retryNeeded_) {
            retryNeeded_ = true;
            retryNeededSinceMs_ = nowMs;
        } else if (nowMs - retryNeededSinceMs_ >= 10000U) {
            recyclePhase_ = RecyclePhase::StopPending;
        }
    } else {
        retryNeeded_ = false;
    }
    if (result == AquaCore::Web::RealtimeRecoveryServiceResult::TransportRecycleSuggested)
        recyclePhase_ = RecyclePhase::StopPending;
    if (recyclePhase_ != RecyclePhase::None) {
        advanceRecycle(nowMs, config);
        return;
    }
    if (!recoveryRequired() ||
        result != AquaCore::Web::RealtimeRecoveryServiceResult::Idle ||
        !isRunning()) return;
    if (cohort_->republishCurrentCohort() !=
        AquaCore::Web::RealtimeCohortPublicationResult::Success) return;
    rememberCoherent();
    (void)tryClear();
}

void LumaRealtime::update(uint32_t nowMs,
                           const AquaCore::Web::WebConfig& webConfig) {
    if (!active_) return;
    serviceRecovery(nowMs, webConfig);
    if (recyclePhase_ != RecyclePhase::None || recoveryRequired() ||
        nowMs - lastPresentationMs_ < PRESENTATION_INTERVAL_MS) return;
    lastPresentationMs_ = nowMs;
    LumaStatusProjection current {};
    if (!app_.buildStatus(current)) {
        (void)status_.invalidate();
        transport_.requestRealtimeRecovery();
        return;
    }
    const bool changed = !havePublished_ ||
        !sameSemanticStatus(current, lastPublished_);
    const auto result = changed ? cohort_->publishTransition() :
        cohort_->republishCurrentCohort();
    if (result == AquaCore::Web::RealtimeCohortPublicationResult::Success ||
        result == AquaCore::Web::RealtimeCohortPublicationResult::NotificationFailure)
        rememberCoherent();
}

} // namespace Web
} // namespace LumaSense
