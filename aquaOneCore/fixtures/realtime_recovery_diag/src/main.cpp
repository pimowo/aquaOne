#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <new>
#include <atomic>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>

#include "secrets.h"
#include "AquaCore/Web/Esp32SnapshotSynchronizer.h"
#include "AquaCore/Web/EspIdfWebTransport.h"
#include "AquaCore/Web/PublishedSnapshot.h"
#include "AquaCore/Web/NativeWebService.h"
#include "AquaCore/Web/RealtimeResync.h"

using namespace AquaCore;
using namespace AquaCore::Web;

namespace {

// Linker wrappers are confined to this PlatformIO environment. A fault is
// armed for one exact dependency call, then consumed atomically.
std::atomic<uint32_t> failQueue {0U};
std::atomic<uint32_t> failSend {0U};
std::atomic<uint32_t> failClose {0U};
std::atomic<uint32_t> failStop {0U};
std::atomic<uint32_t> failBegin {0U};
std::atomic<uint32_t> hitQueue {0U};
std::atomic<uint32_t> hitSend {0U};
std::atomic<uint32_t> hitClose {0U};
std::atomic<uint32_t> hitStop {0U};
std::atomic<uint32_t> hitBegin {0U};
std::atomic<uint32_t> hitBuild {0U};
void observeR4Send(int fd);
void observeR4Close(int fd, esp_err_t result);
std::atomic<uint32_t> serviceRecycleResults {0U};
std::atomic<uint32_t> handledRecycleSuggestions {0U};
std::atomic<uint32_t> probeStop {0U};
std::atomic<uint32_t> probeResult {0U};
uint8_t (*probeDuringStop)() = nullptr;

bool consume(std::atomic<uint32_t>& count) {
    uint32_t current = count.load();
    while (current != 0U) {
        if (count.compare_exchange_weak(current, current - 1U)) return true;
    }
    return false;
}

} // namespace

extern "C" {
esp_err_t __real_httpd_queue_work(httpd_handle_t, httpd_work_fn_t, void*);
esp_err_t __wrap_httpd_queue_work(httpd_handle_t h, httpd_work_fn_t fn, void* arg) {
    if (consume(failQueue)) { hitQueue.fetch_add(1U); return ESP_FAIL; }
    return __real_httpd_queue_work(h, fn, arg);
}
esp_err_t __real_httpd_ws_send_frame_async(httpd_handle_t, int, httpd_ws_frame_t*);
esp_err_t __wrap_httpd_ws_send_frame_async(httpd_handle_t h, int fd, httpd_ws_frame_t* frame) {
    if (consume(failSend)) {
        hitSend.fetch_add(1U);
        observeR4Send(fd);
        return ESP_FAIL;
    }
    return __real_httpd_ws_send_frame_async(h, fd, frame);
}
esp_err_t __real_httpd_sess_trigger_close(httpd_handle_t, int);
esp_err_t __wrap_httpd_sess_trigger_close(httpd_handle_t h, int fd) {
    const bool fail = consume(failClose);
    if (fail) hitClose.fetch_add(1U);
    const esp_err_t result = fail ? ESP_FAIL : __real_httpd_sess_trigger_close(h, fd);
    observeR4Close(fd, result);
    return result;
}
esp_err_t __real_httpd_stop(httpd_handle_t);
esp_err_t __wrap_httpd_stop(httpd_handle_t h) {
    if (consume(probeStop) && probeDuringStop != nullptr) {
        const uint8_t value = probeDuringStop();
        probeResult.store(value);
        if (value == static_cast<uint8_t>(
                AquaCore::Web::RealtimeRecoveryServiceResult::TransportRecycleSuggested))
            serviceRecycleResults.fetch_add(1U);
    }
    if (consume(failStop)) { hitStop.fetch_add(1U); return ESP_FAIL; }
    return __real_httpd_stop(h);
}
esp_err_t __real_httpd_start(httpd_handle_t*, const httpd_config_t*);
esp_err_t __wrap_httpd_start(httpd_handle_t* h, const httpd_config_t* config) {
    if (consume(failBegin)) { hitBegin.fetch_add(1U); return ESP_FAIL; }
    return __real_httpd_start(h, config);
}
}

namespace {

struct HilPayload {
    uint32_t revision;
    uint32_t value;
    uint32_t inverse;
    uint32_t checksum;
};

struct Counters {
    std::atomic<uint32_t> transitions {0U};
    std::atomic<uint32_t> accepted {0U};
    std::atomic<uint32_t> busy {0U};
    std::atomic<uint32_t> queueFailure {0U};
    std::atomic<uint32_t> recoveryRequests {0U};
    std::atomic<uint32_t> recoveryClears {0U};
    std::atomic<uint32_t> serviceIdle {0U};
    std::atomic<uint32_t> serviceProgress {0U};
    std::atomic<uint32_t> serviceRetry {0U};
    std::atomic<uint32_t> serviceRecycle {0U};
    std::atomic<uint32_t> firstBackpressureAttempt {0U};
};

enum class Scenario : uint8_t {
    None, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10
};
enum class Phase : uint8_t {
    Idle, Armed, Recovery, StopPending, RepublishPending,
    BeginPending, Stabilizing, Complete, Failed
};

// Fixed storage: the HTTPD callback copies samples under this short spinlock.
// No sort or HTTP write occurs while the lock is held.
struct Samples {
    uint32_t values[128] {};
    uint32_t count = 0U;
    uint32_t seen = 0U;
    void add(uint32_t value) {
        ++seen;
        values[count % 128U] = value;
        ++count;
    }
};
enum Metric : uint8_t {
    LoopDuration, LoopGap, RecoveryDuration, StopDuration,
    RepublishDuration, BeginDuration, MetricCount
};
portMUX_TYPE metricsLock = portMUX_INITIALIZER_UNLOCKED;
Samples baselineMetrics[MetricCount];
Samples recoveryMetrics[MetricCount];
Samples recycleMetrics[MetricCount];

std::atomic<uint8_t> requestedScenario {0U};
std::atomic<uint8_t> activeScenario {0U};
std::atomic<uint8_t> phase {static_cast<uint8_t>(Phase::Idle)};
std::atomic<uint32_t> scenarioRun {0U};
std::atomic<uint32_t> stopAttempts {0U};
std::atomic<uint32_t> republishAttempts {0U};
std::atomic<uint32_t> beginAttempts {0U};
std::atomic<uint32_t> serviceRetries {0U};
std::atomic<uint32_t> serviceProgresses {0U};
std::atomic<uint32_t> clearFailures {0U};
std::atomic<uint32_t> cleared {0U};
std::atomic<uint32_t> completedCycles {0U};
std::atomic<uint32_t> stopRetryGapMs {0U};
std::atomic<uint32_t> republishRetryGapMs {0U};
std::atomic<uint32_t> beginRetryGapMs {0U};
std::atomic<uint32_t> lastResult {0U};
std::atomic<uint32_t> lastScenarioStartMs {0U};
uint32_t lastLoopUs = 0U;
uint32_t lastRecycleAttemptMs = 0U;
uint32_t retrySinceMs = 0U;
uint32_t stableSinceMs = 0U;
uint32_t lastStopAttemptMs = 0U;
uint32_t lastRepublishAttemptMs = 0U;
uint32_t lastBeginAttemptMs = 0U;
bool retryPending = false;
uint32_t expectedSendHits = 0U;
uint32_t expectedCloseHits = 0U;
uint32_t expectedBusyCount = 0U;
uint32_t expectedQueueHits = 0U;

Samples* metricBucket() {
    const Phase current = static_cast<Phase>(phase.load());
    if (current == Phase::StopPending || current == Phase::RepublishPending ||
        current == Phase::BeginPending) return recycleMetrics;
    if (current == Phase::Recovery || current == Phase::Stabilizing ||
        current == Phase::Armed) return recoveryMetrics;
    return baselineMetrics;
}
void recordMetric(Metric metric, uint32_t us) {
    portENTER_CRITICAL(&metricsLock);
    metricBucket()[metric].add(us);
    portEXIT_CRITICAL(&metricsLock);
}

Esp32SnapshotSynchronizer coreSnapshotSynchronizer;
PublishedSnapshot<CoreSystemProjection> systemSnapshot(coreSnapshotSynchronizer);
PublishedSnapshot<CoreDiagnosticsProjection> diagnosticsSnapshot(coreSnapshotSynchronizer);
WebConfig webConfig;

Esp32SnapshotSynchronizer snapshotSynchronizer;
PublishedSnapshot<RealtimeSnapshot<HilPayload> > snapshot(snapshotSynchronizer);
EspIdfWebTransport transport;
NativeWebService nativeWeb(transport, systemSnapshot, diagnosticsSnapshot);

// Fixture-only access to the real registry and its mutex. This uses the
// standard pointer-to-member test idiom; no Core header or production ABI is
// changed. Every observation of a slot is made under Core's own mutex.
template <typename Tag, typename Tag::type Member> struct ExposeMember {
    friend typename Tag::type member(Tag) { return Member; }
};
struct ClientsMember {
    using type = RealtimeClientRegistry<7U> EspIdfWebTransport::*;
    friend type member(ClientsMember);
};
template struct ExposeMember<ClientsMember, &EspIdfWebTransport::realtimeClients_>;
struct MutexMember {
    using type = SemaphoreHandle_t EspIdfWebTransport::*;
    friend type member(MutexMember);
};
template struct ExposeMember<MutexMember, &EspIdfWebTransport::realtimeMutex_>;

struct R4Trace {
    std::atomic<int> fd {-1};
    std::atomic<uint32_t> slot {UINT32_MAX};
    std::atomic<uint64_t> generation {0U};
    std::atomic<bool> liveBeforeSend {false};
    std::atomic<bool> closingAtClose {false};
    std::atomic<uint32_t> closeAttempts {0U};
    std::atomic<int> firstCloseResult {INT32_MIN};
    std::atomic<int> secondCloseResult {INT32_MIN};
    std::atomic<int> thirdCloseResult {INT32_MIN};
    std::atomic<bool> closeRequested {false};
    std::atomic<bool> released {false};
    std::atomic<uint64_t> replacementGeneration {0U};
} r4;

void resetR4Trace() {
    r4.fd.store(-1);
    r4.slot.store(UINT32_MAX);
    r4.generation.store(0U);
    r4.liveBeforeSend.store(false);
    r4.closingAtClose.store(false);
    r4.closeAttempts.store(0U);
    r4.firstCloseResult.store(INT32_MIN);
    r4.secondCloseResult.store(INT32_MIN);
    r4.thirdCloseResult.store(INT32_MIN);
    r4.closeRequested.store(false);
    r4.released.store(false);
    r4.replacementGeneration.store(0U);
}

void observeR4Send(int fd) {
    if (activeScenario.load() != static_cast<uint8_t>(Scenario::R4)) return;
    SemaphoreHandle_t mutex = transport.*member(MutexMember {});
    if (mutex == nullptr || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;
    auto& clients = transport.*member(ClientsMember {});
    for (size_t i = 0U; i < 7U; ++i) {
        const auto* slot = clients.slot(i);
        if (slot != nullptr && slot->fd == fd &&
            slot->state == RealtimeClientState::Live) {
            r4.fd.store(fd);
            r4.slot.store(static_cast<uint32_t>(i));
            r4.generation.store(slot->generation);
            r4.liveBeforeSend.store(true);
            break;
        }
    }
    xSemaphoreGive(mutex);
}

void observeR4Close(int fd, esp_err_t result) {
    if (activeScenario.load() != static_cast<uint8_t>(Scenario::R4) ||
        fd != r4.fd.load()) return;
    SemaphoreHandle_t mutex = transport.*member(MutexMember {});
    if (mutex == nullptr || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;
    auto& clients = transport.*member(ClientsMember {});
    const uint32_t index = r4.slot.load();
    const auto* slot = clients.slot(index);
    if (slot != nullptr && slot->generation == r4.generation.load() &&
        slot->fd == fd && slot->state == RealtimeClientState::Closing)
        r4.closingAtClose.store(true);
    xSemaphoreGive(mutex);
    const uint32_t attempt = r4.closeAttempts.fetch_add(1U) + 1U;
    if (attempt == 1U) r4.firstCloseResult.store(result);
    else if (attempt == 2U) r4.secondCloseResult.store(result);
    else if (attempt == 3U) r4.thirdCloseResult.store(result);
}

void observeR4Cleanup() {
    if (activeScenario.load() != static_cast<uint8_t>(Scenario::R4) ||
        r4.generation.load() == 0U) return;
    SemaphoreHandle_t mutex = transport.*member(MutexMember {});
    if (mutex == nullptr || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;
    auto& clients = transport.*member(ClientsMember {});
    const auto* slot = clients.slot(r4.slot.load());
    if (slot != nullptr) {
        if (slot->state == RealtimeClientState::Free ||
            slot->generation != r4.generation.load()) {
            r4.released.store(true);
            if (slot->state != RealtimeClientState::Free)
                r4.replacementGeneration.store(slot->generation);
        } else if (slot->closeRequested) {
            r4.closeRequested.store(true);
        }
    }
    xSemaphoreGive(mutex);
}

uint8_t probeRecoveryDuringStop() {
    const RealtimeRecoveryServiceResult result = transport.serviceRealtimeRecovery();
    if (result == RealtimeRecoveryServiceResult::TransportRecycleSuggested) {
        // This callback is invoked by the fixture's stop wrapper on the
        // Application stack, while Core has temporarily nulled server_.
        handledRecycleSuggestions.fetch_add(1U);
        phase.store(static_cast<uint8_t>(Phase::StopPending));
    }
    return static_cast<uint8_t>(result);
}
Identity::RuntimeIdentity runtimeIdentity;
RealtimeResyncResourceBinding resourceBinding;
alignas(RealtimeStreamSequencer) uint8_t sequencerStorage[sizeof(RealtimeStreamSequencer)];
alignas(RealtimeCohortPublisher<1U>) uint8_t publisherStorage[sizeof(RealtimeCohortPublisher<1U>)];
RealtimeStreamSequencer* sequencer = nullptr;
RealtimeCohortPublisher<1U>* publisher = nullptr;
RealtimeSnapshot<HilPayload> candidate;
HilPayload authoritative {0U, 0x12345678U, ~0x12345678U, 0U};
Counters counters;

std::atomic<uint32_t> pendingChanges {0U};
std::atomic<uint32_t> pendingBurst {0U};
std::atomic<bool> failNextBuild {false};
std::atomic<uint8_t> lastPublicationResult {
    static_cast<uint8_t>(RealtimePublicationResult::Accepted)
};
uint32_t nextServiceAt = 0U;

uint32_t payloadChecksum(const HilPayload& payload) {
    return payload.revision ^ payload.value ^ payload.inverse ^ 0xA55A3CC3U;
}

bool invalidateResource(void*) {
    return snapshot.invalidate();
}

bool buildResource(void*, Identity::RuntimeIdentity identity,
                   RealtimeStreamPosition position) {
    if (failNextBuild.exchange(false)) {
        hitBuild.fetch_add(1U);
        return false;
    }
    authoritative.inverse = ~authoritative.value;
    authoritative.checksum = payloadChecksum(authoritative);
    return RealtimeSnapshot<HilPayload>::coherent(
        identity, position, authoritative, candidate
    );
}

bool publishResource(void*) {
    return snapshot.publish(candidate);
}

bool publishStreamState(void*, const RealtimeStreamStartState& state) {
    return transport.publishRealtimeStreamStartState(state);
}

RealtimePublicationResult submitNotification(
    void*, const RealtimeNotificationMetadata& metadata
) {
    char frame[256U] {};
    const int length = snprintf(frame, sizeof(frame),
        "{\"type\":\"notification\",\"runtime_id\":\"%016llX\","
        "\"sequence\":\"%llu\",\"kind\":\"luma_status_changed\"}",
        static_cast<unsigned long long>(metadata.runtimeIdentity().value()),
        static_cast<unsigned long long>(metadata.sequence().value()));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(frame))
        return RealtimePublicationResult::PayloadTooLarge;
    const RealtimePublicationResult result = transport.publishRealtime(
        metadata, RealtimeFrameType::Text,
        reinterpret_cast<const uint8_t*>(frame), static_cast<size_t>(length)
    );
    lastPublicationResult.store(static_cast<uint8_t>(result));
    if (result == RealtimePublicationResult::Accepted) {
        counters.accepted.fetch_add(1U);
    } else if (result == RealtimePublicationResult::Busy) {
        counters.busy.fetch_add(1U);
    } else if (result == RealtimePublicationResult::QueueWorkFailure) {
        counters.queueFailure.fetch_add(1U);
    }
    return result;
}

void requestRecovery(void*) {
    counters.recoveryRequests.fetch_add(1U);
    transport.requestRealtimeRecovery();
}

bool writeJson(WebResponseWriter& response, uint16_t status, const char* json) {
    return response.beginResponse(status, ContentType::Json) &&
        response.writeText(json) && response.endResponse();
}

void snapshotRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    RealtimeSnapshot<HilPayload> copy;
    if (!snapshot.read(copy) || !copy.isCoherentCurrent()) {
        writeJson(response, 503U, "{\"available\":false}");
        return;
    }
    const HilPayload& payload = copy.payload();
    char body[512U] {};
    snprintf(
        body, sizeof(body),
        "{\"available\":true,\"coherent\":true,\"runtime\":\"%016llx\","
        "\"kind\":\"%s\",\"sequence\":%llu,\"revision\":%lu,"
        "\"value\":%lu,\"inverse\":%lu,\"checksum\":%lu}",
        static_cast<unsigned long long>(copy.runtimeIdentity().value()),
        copy.watermark().isBeforeFirst() ? "before-first" : "at",
        static_cast<unsigned long long>(copy.watermark().hasSequence() ?
            copy.watermark().sequence().value() : 0U),
        static_cast<unsigned long>(payload.revision),
        static_cast<unsigned long>(payload.value),
        static_cast<unsigned long>(payload.inverse),
        static_cast<unsigned long>(payload.checksum)
    );
    writeJson(response, 200U, body);
}

void statusRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    RealtimeSnapshot<HilPayload> copy;
    const bool available = snapshot.read(copy) && copy.isCoherentCurrent();
    const RealtimeStreamPosition position = available ? copy.watermark() :
        RealtimeStreamPosition::beforeFirst();
    char body[1024U] {};
    snprintf(
        body, sizeof(body),
        "{\"runtime\":\"%016llx\",\"kind\":\"%s\",\"sequence\":%llu,"
        "\"recovery\":%s,\"coherent\":%s,\"revision\":%lu,"
        "\"transitions\":%lu,\"accepted\":%lu,\"busy\":%lu,"
        "\"queueFailure\":%lu,\"firstBackpressureAttempt\":%lu,"
        "\"recoveryRequests\":%lu,\"recoveryClears\":%lu,"
        "\"serviceIdle\":%lu,\"serviceProgress\":%lu,"
        "\"serviceRetry\":%lu,\"serviceRecycle\":%lu,"
        "\"lastPublicationResult\":%u,"
        "\"heapFreeInternal\":%u,\"heapMinInternal\":%u,"
        "\"heapLargestInternal\":%u}",
        static_cast<unsigned long long>(runtimeIdentity.value()),
        position.isBeforeFirst() ? "before-first" : "at",
        static_cast<unsigned long long>(position.hasSequence() ?
            position.sequence().value() : 0U),
        transport.isRealtimeRecoveryRequired() ? "true" : "false",
        available ? "true" : "false",
        static_cast<unsigned long>(available ? copy.payload().revision : 0U),
        static_cast<unsigned long>(counters.transitions.load()),
        static_cast<unsigned long>(counters.accepted.load()),
        static_cast<unsigned long>(counters.busy.load()),
        static_cast<unsigned long>(counters.queueFailure.load()),
        static_cast<unsigned long>(counters.firstBackpressureAttempt.load()),
        static_cast<unsigned long>(counters.recoveryRequests.load()),
        static_cast<unsigned long>(counters.recoveryClears.load()),
        static_cast<unsigned long>(counters.serviceIdle.load()),
        static_cast<unsigned long>(counters.serviceProgress.load()),
        static_cast<unsigned long>(counters.serviceRetry.load()),
        static_cast<unsigned long>(counters.serviceRecycle.load()),
        static_cast<unsigned>(lastPublicationResult.load()),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)
    );
    writeJson(response, 200U, body);
}

struct HeapSample {
    uint32_t free = 0U;
    uint32_t minimum = 0U;
    uint32_t largest = 0U;
};
HeapSample beforeHeap, afterHeap, stableHeap;
HeapSample heapNow() {
    HeapSample h;
    h.free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    h.minimum = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    h.largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    return h;
}

void diagnosticsRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    // HTTPD dispatch is serial; keep this bounded diagnostic buffer off its stack.
    observeR4Cleanup();
    static char body[2048U] {};
    const HeapSample current = heapNow();
    HeapSample before, after, stable;
    portENTER_CRITICAL(&metricsLock);
    before = beforeHeap;
    after = afterHeap;
    stable = stableHeap;
    portEXIT_CRITICAL(&metricsLock);
    const int length = snprintf(body, sizeof(body),
        "{\"scenario\":%u,\"phase\":%u,\"run\":%lu,\"result\":%lu,"
        "\"stopAttempts\":%lu,\"republishAttempts\":%lu,\"beginAttempts\":%lu,"
        "\"serviceRetries\":%lu,\"serviceProgresses\":%lu,\"serviceRecycleResults\":%lu,\"handledRecycleSuggestions\":%lu,"
        "\"clearFailures\":%lu,\"cleared\":%lu,\"completedCycles\":%lu,\"stopProbeResult\":%lu,"
        "\"retryGapMs\":{\"stop\":%lu,\"republish\":%lu,\"begin\":%lu},"
        "\"injected\":{\"queue\":%lu,\"send\":%lu,\"close\":%lu,\"stop\":%lu,\"begin\":%lu,\"build\":%lu},"
        "\"recovery\":%s,\"running\":%s,\"heap\":{\"free\":%lu,\"min\":%lu,\"largest\":%lu},"
        "\"beforeHeap\":[%lu,%lu,%lu],\"afterHeap\":[%lu,%lu,%lu],\"stableHeap\":[%lu,%lu,%lu],"
        "\"r4\":{\"fd\":%d,\"slot\":%lu,\"generation\":\"%llu\","
        "\"liveBeforeSend\":%s,\"closingAtClose\":%s,\"closeAttempts\":%lu,"
        "\"closeResults\":[%d,%d,%d],\"closeRequested\":%s,"
        "\"released\":%s,\"replacementGeneration\":\"%llu\"}}",
        static_cast<unsigned>(activeScenario.load()), static_cast<unsigned>(phase.load()),
        static_cast<unsigned long>(scenarioRun.load()), static_cast<unsigned long>(lastResult.load()),
        static_cast<unsigned long>(stopAttempts.load()),
        static_cast<unsigned long>(republishAttempts.load()),
        static_cast<unsigned long>(beginAttempts.load()),
        static_cast<unsigned long>(serviceRetries.load()),
        static_cast<unsigned long>(serviceProgresses.load()),
        static_cast<unsigned long>(serviceRecycleResults.load()),
        static_cast<unsigned long>(handledRecycleSuggestions.load()),
        static_cast<unsigned long>(clearFailures.load()),
        static_cast<unsigned long>(cleared.load()),
        static_cast<unsigned long>(completedCycles.load()),
        static_cast<unsigned long>(probeResult.load()),
        static_cast<unsigned long>(stopRetryGapMs.load()),
        static_cast<unsigned long>(republishRetryGapMs.load()),
        static_cast<unsigned long>(beginRetryGapMs.load()),
        static_cast<unsigned long>(hitQueue.load()), static_cast<unsigned long>(hitSend.load()),
        static_cast<unsigned long>(hitClose.load()), static_cast<unsigned long>(hitStop.load()),
        static_cast<unsigned long>(hitBegin.load()), static_cast<unsigned long>(hitBuild.load()),
        transport.isRealtimeRecoveryRequired() ? "true" : "false",
        transport.isRunning() ? "true" : "false",
        static_cast<unsigned long>(current.free), static_cast<unsigned long>(current.minimum),
        static_cast<unsigned long>(current.largest),
        static_cast<unsigned long>(before.free), static_cast<unsigned long>(before.minimum),
        static_cast<unsigned long>(before.largest),
        static_cast<unsigned long>(after.free), static_cast<unsigned long>(after.minimum),
        static_cast<unsigned long>(after.largest),
        static_cast<unsigned long>(stable.free), static_cast<unsigned long>(stable.minimum),
        static_cast<unsigned long>(stable.largest),
        r4.fd.load(), static_cast<unsigned long>(r4.slot.load()),
        static_cast<unsigned long long>(r4.generation.load()),
        r4.liveBeforeSend.load() ? "true" : "false",
        r4.closingAtClose.load() ? "true" : "false",
        static_cast<unsigned long>(r4.closeAttempts.load()),
        r4.firstCloseResult.load(), r4.secondCloseResult.load(),
        r4.thirdCloseResult.load(),
        r4.closeRequested.load() ? "true" : "false",
        r4.released.load() ? "true" : "false",
        static_cast<unsigned long long>(r4.replacementGeneration.load()));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(body)) {
        writeJson(response, 500U, "{\"error\":\"diagnostics_overflow\"}");
        return;
    }
    writeJson(response, 200U, body);
}

void metricsRoute(void*, const HttpRouteRequest& request, WebResponseWriter& response) {
    Samples* bucket = baselineMetrics;
    const char* context = "baseline";
    if (strcmp(request.path, "/api/diag/recovery-metrics") == 0) {
        bucket = recoveryMetrics;
        context = "recovery";
    } else if (strcmp(request.path, "/api/diag/recycle-metrics") == 0) {
        bucket = recycleMetrics;
        context = "recycle";
    }
    if (!response.beginResponse(200U, ContentType::Json)) return;
    response.writeText("{\"unit\":\"us\",\"context\":\"");
    response.writeText(context);
    response.writeText("\",\"metrics\":[");
    static const char* names[] = {"fixture_loop", "fixture_loop_gap",
        "service_realtime_recovery", "try_stop", "republish", "native_web_begin"};
    for (size_t metric = 0U; metric < MetricCount; ++metric) {
        uint32_t local[128U] {};
        uint32_t count, seen;
        portENTER_CRITICAL(&metricsLock);
        count = bucket[metric].count < 128U ? bucket[metric].count : 128U;
        seen = bucket[metric].seen;
        memcpy(local, bucket[metric].values, count * sizeof(uint32_t));
        portEXIT_CRITICAL(&metricsLock);
        std::sort(local, local + count);
        char item[160U] {};
        snprintf(item, sizeof(item),
            "%s{\"name\":\"%s\",\"count\":%lu,\"retained\":%lu,"
            "\"min\":%lu,\"median\":%lu,\"max\":%lu}",
            metric == 0U ? "" : ",", names[metric],
            static_cast<unsigned long>(seen), static_cast<unsigned long>(count),
            static_cast<unsigned long>(count ? local[0] : 0U),
            static_cast<unsigned long>(count ? local[count / 2U] : 0U),
            static_cast<unsigned long>(count ? local[count - 1U] : 0U));
        response.writeText(item);
    }
    response.writeText("]}");
    response.endResponse();
}

void runRoute(void*, const HttpRouteRequest& request, WebResponseWriter& response) {
    const bool valid = request.body != nullptr &&
        ((request.bodyLength == 2U && request.body[0] == 'R' &&
          request.body[1] >= '1' && request.body[1] <= '9') ||
         (request.bodyLength == 3U && memcmp(request.body, "R10", 3U) == 0));
    if (!valid || requestedScenario.load() != 0U ||
        (phase.load() != static_cast<uint8_t>(Phase::Idle) &&
         phase.load() != static_cast<uint8_t>(Phase::Complete))) {
        writeJson(response, 409U, "{\"accepted\":false}");
        return;
    }
    const uint8_t id = request.bodyLength == 3U ? 10U :
        static_cast<uint8_t>(request.body[1] - '0');
    requestedScenario.store(id);
    if (id == 1U) delay(750U); // Only R1 holds HTTPD while the four work slots fill.
    writeJson(response, 202U, "{\"accepted\":true}");
}

void acceptedRoute(WebResponseWriter& response) {
    writeJson(response, 202U, "{\"accepted\":true}");
}

void changeRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    pendingChanges.fetch_add(1U);
    acceptedRoute(response);
}

void applyOneTransition(uint32_t attempt) {
    ++authoritative.revision;
    authoritative.value = 0x12345678U + authoritative.revision * 17U;
    counters.transitions.fetch_add(1U);
    const RealtimeCohortPublicationResult result = publisher->publishTransition();
    if (result != RealtimeCohortPublicationResult::Success &&
        counters.firstBackpressureAttempt.load() == 0U &&
        (lastPublicationResult.load() ==
             static_cast<uint8_t>(RealtimePublicationResult::Busy) ||
         lastPublicationResult.load() ==
             static_cast<uint8_t>(RealtimePublicationResult::QueueWorkFailure))) {
        counters.firstBackpressureAttempt.store(attempt);
    }
}

void serviceTransport() {
    const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
    const RealtimeRecoveryServiceResult result = transport.serviceRealtimeRecovery();
    recordMetric(RecoveryDuration,
        static_cast<uint32_t>(esp_timer_get_time()) - started);
    observeR4Cleanup();
    switch (result) {
        case RealtimeRecoveryServiceResult::Idle:
            counters.serviceIdle.fetch_add(1U);
            break;
        case RealtimeRecoveryServiceResult::Progress:
            counters.serviceProgress.fetch_add(1U);
            serviceProgresses.fetch_add(1U);
            break;
        case RealtimeRecoveryServiceResult::RetryNeeded:
            counters.serviceRetry.fetch_add(1U);
            serviceRetries.fetch_add(1U);
            break;
        case RealtimeRecoveryServiceResult::TransportRecycleSuggested:
            counters.serviceRecycle.fetch_add(1U);
            serviceRecycleResults.fetch_add(1U);
            break;
    }
    const Phase current = static_cast<Phase>(phase.load());
    if (current != Phase::Recovery && current != Phase::Armed) return;
    const uint32_t now = millis();
    if (result == RealtimeRecoveryServiceResult::RetryNeeded) {
        if (!retryPending) { retryPending = true; retrySinceMs = now; }
        else if (now - retrySinceMs >= 10000U) {
            phase.store(static_cast<uint8_t>(Phase::StopPending));
            lastRecycleAttemptMs = now - 1000U;
        }
    } else retryPending = false;
    if (result == RealtimeRecoveryServiceResult::TransportRecycleSuggested) {
        handledRecycleSuggestions.fetch_add(1U);
        phase.store(static_cast<uint8_t>(Phase::StopPending));
        lastRecycleAttemptMs = now - 1000U;
    }
    if (phase.load() != static_cast<uint8_t>(Phase::Recovery)) return;
    if (result != RealtimeRecoveryServiceResult::Idle) return;
    if (transport.isRealtimeRecoveryRequired()) {
        const uint32_t republishStarted = static_cast<uint32_t>(esp_timer_get_time());
        const bool ok = publisher->republishCurrentCohort() ==
            RealtimeCohortPublicationResult::Success;
        recordMetric(RepublishDuration,
            static_cast<uint32_t>(esp_timer_get_time()) - republishStarted);
        republishAttempts.fetch_add(1U);
        if (!ok) return;
        if (!transport.tryClearRealtimeRecovery(
                runtimeIdentity, publisher->coherentPosition())) {
            clearFailures.fetch_add(1U);
            return;
        }
        cleared.fetch_add(1U);
    }
    const Scenario scenario = static_cast<Scenario>(activeScenario.load());
    if (scenario == Scenario::R4 || scenario == Scenario::R5) {
        if (hitSend.load() < expectedSendHits ||
            hitClose.load() < expectedCloseHits) return;
    }
    if (scenario == Scenario::R1 && counters.busy.load() < expectedBusyCount)
        return;
    if (scenario == Scenario::R2 && hitQueue.load() < expectedQueueHits)
        return;
    stableSinceMs = now;
    const HeapSample after = heapNow();
    portENTER_CRITICAL(&metricsLock);
    afterHeap = after;
    portEXIT_CRITICAL(&metricsLock);
    phase.store(static_cast<uint8_t>(Phase::Stabilizing));
}

void startScenario(Scenario scenario) {
    resetR4Trace();
    activeScenario.store(static_cast<uint8_t>(scenario));
    scenarioRun.fetch_add(1U);
    lastScenarioStartMs.store(millis());
    phase.store(static_cast<uint8_t>(Phase::Armed));
    lastResult.store(0U);
    const HeapSample before = heapNow();
    portENTER_CRITICAL(&metricsLock);
    beforeHeap = before;
    afterHeap = HeapSample();
    stableHeap = HeapSample();
    portEXIT_CRITICAL(&metricsLock);
    retryPending = false;
    lastStopAttemptMs = 0U;
    lastRepublishAttemptMs = 0U;
    lastBeginAttemptMs = 0U;
    stopRetryGapMs.store(0U);
    republishRetryGapMs.store(0U);
    beginRetryGapMs.store(0U);
    expectedSendHits = hitSend.load() + 1U;
    expectedCloseHits = hitClose.load() + 1U;
    expectedBusyCount = counters.busy.load() + 1U;
    expectedQueueHits = hitQueue.load() + 1U;
    if (scenario == Scenario::R1) pendingBurst.store(32U);
    else if (scenario == Scenario::R2) {
        failQueue.store(1U);
        pendingChanges.store(1U);
    } else if (scenario == Scenario::R3) {
        transport.requestRealtimeRecovery();
    } else if (scenario == Scenario::R4 || scenario == Scenario::R5) {
        failSend.store(1U);
        // The HTTPD send callback consumes the first close failure. A second
        // failure makes the Application service observe RetryNeeded before a
        // later successful close, without changing Core behavior.
        failClose.store(scenario == Scenario::R5 ? 200U : 2U);
        if (scenario == Scenario::R5) probeStop.store(1U);
        pendingChanges.store(1U);
    } else {
        if (scenario == Scenario::R6) failStop.store(1U);
        if (scenario == Scenario::R8) failBegin.store(1U);
        phase.store(static_cast<uint8_t>(Phase::StopPending));
        lastRecycleAttemptMs = millis() - 1000U;
        return;
    }
    phase.store(static_cast<uint8_t>(Phase::Recovery));
}

void advanceRecycle() {
    const uint32_t now = millis();
    if (now - lastRecycleAttemptMs < 1000U) return;
    lastRecycleAttemptMs = now;
    Phase current = static_cast<Phase>(phase.load());
    if (current == Phase::StopPending) {
        if (lastStopAttemptMs != 0U) stopRetryGapMs.store(now - lastStopAttemptMs);
        lastStopAttemptMs = now;
        stopAttempts.fetch_add(1U);
        const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
        const bool stopped = transport.tryStop();
        recordMetric(StopDuration,
            static_cast<uint32_t>(esp_timer_get_time()) - started);
        if (!stopped) return;
        failClose.store(0U); // no stale fault after HTTPD is gone
        phase.store(static_cast<uint8_t>(Phase::RepublishPending));
        current = Phase::RepublishPending;
        if (activeScenario.load() == static_cast<uint8_t>(Scenario::R7))
            failNextBuild.store(true);
    }
    if (current == Phase::RepublishPending) {
        if (lastRepublishAttemptMs != 0U)
            republishRetryGapMs.store(now - lastRepublishAttemptMs);
        lastRepublishAttemptMs = now;
        republishAttempts.fetch_add(1U);
        const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
        const bool republishOk = publisher->republishCurrentCohort() ==
            RealtimeCohortPublicationResult::Success;
        recordMetric(RepublishDuration,
            static_cast<uint32_t>(esp_timer_get_time()) - started);
        if (!republishOk) return;
        phase.store(static_cast<uint8_t>(Phase::BeginPending));
        current = Phase::BeginPending;
    }
    if (current == Phase::BeginPending) {
        if (lastBeginAttemptMs != 0U)
            beginRetryGapMs.store(now - lastBeginAttemptMs);
        lastBeginAttemptMs = now;
        beginAttempts.fetch_add(1U);
        const uint32_t started = static_cast<uint32_t>(esp_timer_get_time());
        const bool begun = nativeWeb.begin(webConfig) && nativeWeb.isRunning();
        recordMetric(BeginDuration,
            static_cast<uint32_t>(esp_timer_get_time()) - started);
        if (!begun) return;
        if (transport.isRealtimeRecoveryRequired()) {
            if (!transport.tryClearRealtimeRecovery(
                    runtimeIdentity, publisher->coherentPosition())) {
                clearFailures.fetch_add(1U);
                return;
            }
            cleared.fetch_add(1U);
        }
        stableSinceMs = now;
        const HeapSample after = heapNow();
        portENTER_CRITICAL(&metricsLock);
        afterHeap = after;
        portEXIT_CRITICAL(&metricsLock);
        phase.store(static_cast<uint8_t>(Phase::Stabilizing));
        completedCycles.fetch_add(1U);
    }
}

void serviceScenario() {
    const uint8_t pending = requestedScenario.exchange(0U);
    if (pending != 0U) startScenario(static_cast<Scenario>(pending));
    const Phase current = static_cast<Phase>(phase.load());
    if (current == Phase::StopPending || current == Phase::RepublishPending ||
        current == Phase::BeginPending) advanceRecycle();
    else if (current == Phase::Stabilizing && millis() - stableSinceMs >= 500U) {
        if (nativeWeb.isRunning() && !transport.isRealtimeRecoveryRequired() &&
            publisher->isCoherentAt(runtimeIdentity, sequencer->currentPosition())) {
            const HeapSample stable = heapNow();
            portENTER_CRITICAL(&metricsLock);
            stableHeap = stable;
            portEXIT_CRITICAL(&metricsLock);
            lastResult.store(1U);
            phase.store(static_cast<uint8_t>(Phase::Complete));
        } else {
            lastResult.store(2U);
            phase.store(static_cast<uint8_t>(Phase::Failed));
        }
    }
    if (current != Phase::Idle && current != Phase::Complete &&
        current != Phase::Failed &&
        millis() - lastScenarioStartMs.load() > 30000U) {
        lastResult.store(2U);
        phase.store(static_cast<uint8_t>(Phase::Failed));
    }
}

bool initializeRuntime() {
    uint64_t value = (static_cast<uint64_t>(esp_random()) << 32U) | esp_random();
    if (value == 0U) value = 1U;
    return Identity::RuntimeIdentity::fromValue(value, runtimeIdentity);
}

void printHardwareEvidence() {
    Serial.printf(
        "HIL_HW chip=%s revision=%u flash=%u psram=%u idf=%s sdk=%s\n",
        ESP.getChipModel(), ESP.getChipRevision(), ESP.getFlashChipSize(),
        ESP.getPsramSize(), esp_get_idf_version(), ESP.getSdkVersion()
    );
}

} // namespace

void setup() {
    probeDuringStop = probeRecoveryDuringStop;
    Serial.begin(115200);
    delay(500U);
    printHardwareEvidence();
    if (!initializeRuntime()) {
        Serial.println("HIL_FATAL runtime");
        return;
    }
    authoritative.checksum = payloadChecksum(authoritative);
    sequencer = new (sequencerStorage) RealtimeStreamSequencer(runtimeIdentity);
    resourceBinding = RealtimeResyncResourceBinding(
        nullptr, invalidateResource, buildResource, publishResource
    );
    publisher = new (publisherStorage) RealtimeCohortPublisher<1U>(
        runtimeIdentity, *sequencer, &resourceBinding, 1U,
        publishStreamState, nullptr, submitNotification, nullptr,
        requestRecovery, nullptr
    );

    webConfig.enabled = true;
    webConfig.port = 80U;
    transport.addRoute("/api/hil/snapshot", HttpMethod::Get, snapshotRoute);
    transport.addRoute("/api/hil/status", HttpMethod::Get, statusRoute);
    transport.addRoute("/api/diag/status", HttpMethod::Get, diagnosticsRoute);
    transport.addRoute("/api/diag/baseline-metrics", HttpMethod::Get, metricsRoute);
    transport.addRoute("/api/diag/recovery-metrics", HttpMethod::Get, metricsRoute);
    transport.addRoute("/api/diag/recycle-metrics", HttpMethod::Get, metricsRoute);
    transport.addRoute("/api/diag/run", HttpMethod::Post, runRoute, nullptr,
                       HttpRouteOptions {3U});
    transport.addRoute("/api/diag/change", HttpMethod::Post, changeRoute);
    transport.setRealtimeEndpoint("/ws/hil");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    for (uint8_t attempt = 0U;
         attempt < 6U && WiFi.status() != WL_CONNECTED; ++attempt) {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        const uint32_t deadline = millis() + 15000U;
        while (WiFi.status() != WL_CONNECTED &&
               static_cast<int32_t>(deadline - millis()) > 0) {
            delay(100U);
        }
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.disconnect(false, false);
            delay(1000U);
        }
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("HIL_FATAL wifi");
        return;
    }
    if (publisher->publishBaseline() != RealtimeCohortPublicationResult::Success ||
        !nativeWeb.begin(webConfig)) {
        Serial.println("HIL_FATAL server");
        return;
    }
    Serial.printf(
        "HIL_READY ip=%s runtime=%016llx heap=%u min=%u largest=%u\n",
        WiFi.localIP().toString().c_str(),
        static_cast<unsigned long long>(runtimeIdentity.value()),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)
    );
}

void loop() {
    const uint32_t loopStarted = static_cast<uint32_t>(esp_timer_get_time());
    if (lastLoopUs != 0U) recordMetric(LoopGap, loopStarted - lastLoopUs);
    lastLoopUs = loopStarted;
    serviceScenario();
    if (static_cast<int32_t>(millis() - nextServiceAt) >= 0) {
        if (phase.load() != static_cast<uint8_t>(Phase::StopPending) &&
            phase.load() != static_cast<uint8_t>(Phase::RepublishPending) &&
            phase.load() != static_cast<uint8_t>(Phase::BeginPending))
            serviceTransport();
        nextServiceAt = millis() + 100U;
    }

    uint32_t changes = pendingChanges.exchange(0U);
    for (uint32_t i = 1U; i <= changes; ++i) applyOneTransition(i);

    const uint32_t burst = pendingBurst.exchange(0U);
    for (uint32_t i = 1U; i <= burst; ++i) {
        applyOneTransition(i);
        if (transport.isRealtimeRecoveryRequired()) break;
    }

    recordMetric(LoopDuration,
        static_cast<uint32_t>(esp_timer_get_time()) - loopStarted);
    delay(1U);
}
