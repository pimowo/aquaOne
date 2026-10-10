#pragma once

#include <new>
#include <type_traits>

#include "AquaCore/Web/RealtimeResync.h"
#include "LumaNativeWeb.h"

namespace LumaSense {
namespace Web {

// All callbacks and storage belong to the composition root for the lifetime
// of HTTPD. The only writer is the Application loop.
class LumaRealtime {
public:
    static constexpr uint32_t PRESENTATION_INTERVAL_MS = 100U;
    static constexpr uint32_t RECOVERY_INTERVAL_MS = 100U;
    static constexpr uint32_t RECYCLE_RETRY_MS = 1000U;
    static constexpr size_t NOTIFICATION_MAX_LENGTH = 118U;

    // Optional deterministic lifecycle port for product tests. Production
    // uses the borrowed Core transport and NativeWebService directly.
    struct LifecyclePort {
        void* context = nullptr;
        bool (*recoveryRequired)(void*) = nullptr;
        AquaCore::Web::RealtimeRecoveryServiceResult (*serviceRecovery)(void*) = nullptr;
        bool (*tryStop)(void*) = nullptr;
        bool (*begin)(void*, const AquaCore::Web::WebConfig&) = nullptr;
        bool (*isRunning)(void*) = nullptr;
        bool (*tryClear)(void*, AquaCore::Identity::RuntimeIdentity,
                         AquaCore::Web::RealtimeStreamPosition) = nullptr;
    };

    LumaRealtime(LumaWebApplication& app,
                 AquaCore::Web::PublishedSnapshot<LumaPublishedStatus>& status,
                 AquaCore::Web::EspIdfWebTransport& transport,
                 AquaCore::Web::NativeWebService& service);
    LumaRealtime(LumaWebApplication& app,
                 AquaCore::Web::PublishedSnapshot<LumaPublishedStatus>& status,
                 AquaCore::Web::EspIdfWebTransport& transport,
                 AquaCore::Web::NativeWebService& service,
                 LifecyclePort lifecycle);
    ~LumaRealtime();
    LumaRealtime(const LumaRealtime&) = delete;
    LumaRealtime& operator=(const LumaRealtime&) = delete;

    // A false result leaves the caller free to publish plain HTTP status.
    bool start(AquaCore::Identity::RuntimeIdentity runtime);
    // Called once after route registration and the listener begin attempt.
    // An unsuccessful startup permanently selects plain polling this runtime.
    bool confirmStartup(bool wsRegistered, bool listenerRunning) {
        active_ = active_ && wsRegistered && listenerRunning;
        return active_;
    }
    void update(uint32_t nowMs, const AquaCore::Web::WebConfig& webConfig);
    bool active() const { return active_; }

    static bool encodeNotification(
        const AquaCore::Web::RealtimeNotificationMetadata& metadata,
        uint8_t* output, size_t capacity, size_t& length);
    static bool sameSemanticStatus(const LumaStatusProjection& left,
                                   const LumaStatusProjection& right);

private:
    typedef AquaCore::Web::RealtimeCohortPublisher<1U> Cohort;
    static bool invalidateResource(void* context);
    static bool buildResource(void* context,
        AquaCore::Identity::RuntimeIdentity runtime,
        AquaCore::Web::RealtimeStreamPosition position);
    static bool publishResource(void* context);
    static bool publishStream(void* context,
        const AquaCore::Web::RealtimeStreamStartState& state);
    static AquaCore::Web::RealtimePublicationResult notify(void* context,
        const AquaCore::Web::RealtimeNotificationMetadata& metadata);
    static void requestRecovery(void* context);
    void rememberCoherent();
    void serviceRecovery(uint32_t nowMs, const AquaCore::Web::WebConfig& config);
    bool recoveryRequired() const;
    AquaCore::Web::RealtimeRecoveryServiceResult serviceClientClosures();
    bool tryStop();
    bool begin(const AquaCore::Web::WebConfig& config);
    bool isRunning() const;
    bool tryClear();
    void advanceRecycle(uint32_t nowMs, const AquaCore::Web::WebConfig& config);

    enum class RecyclePhase : uint8_t {
        None,
        StopPending,
        RepublishPending,
        RestartPending
    };

    LumaWebApplication& app_;
    AquaCore::Web::PublishedSnapshot<LumaPublishedStatus>& status_;
    AquaCore::Web::EspIdfWebTransport& transport_;
    AquaCore::Web::NativeWebService& service_;
    LifecyclePort lifecycle_ {};
    AquaCore::Identity::RuntimeIdentity runtime_ {};
    typename std::aligned_storage<sizeof(AquaCore::Web::RealtimeStreamSequencer),
        alignof(AquaCore::Web::RealtimeStreamSequencer)>::type sequencerStorage_;
    typename std::aligned_storage<sizeof(Cohort), alignof(Cohort)>::type cohortStorage_;
    AquaCore::Web::RealtimeStreamSequencer* sequencer_ = nullptr;
    Cohort* cohort_ = nullptr;
    AquaCore::Web::RealtimeResyncResourceBinding resource_ {};
    LumaPublishedStatus pending_ {};
    LumaStatusProjection lastPublished_ {};
    bool havePublished_ = false;
    bool active_ = false;
    RecyclePhase recyclePhase_ = RecyclePhase::None;
    bool retryNeeded_ = false;
    uint32_t lastPresentationMs_ = 0U;
    uint32_t lastRecoveryMs_ = 0U;
    uint32_t lastRecycleAttemptMs_ = 0U;
    uint32_t retryNeededSinceMs_ = 0U;
};

} // namespace Web
} // namespace LumaSense
