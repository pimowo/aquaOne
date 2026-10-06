#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <new>
#include <atomic>

#include "secrets.h"
#include "AquaCore/Web/Esp32SnapshotSynchronizer.h"
#include "AquaCore/Web/EspIdfWebTransport.h"
#include "AquaCore/Web/PublishedSnapshot.h"
#include "AquaCore/Web/RealtimeResync.h"

using namespace AquaCore;
using namespace AquaCore::Web;

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

Esp32SnapshotSynchronizer snapshotSynchronizer;
PublishedSnapshot<RealtimeSnapshot<HilPayload> > snapshot(snapshotSynchronizer);
EspIdfWebTransport transport;
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
std::atomic<uint32_t> pendingPaced {0U};
std::atomic<bool> failNextBuild {false};
std::atomic<bool> failNextNotification {false};
std::atomic<bool> recoveryRequested {false};
std::atomic<bool> recoveryRepublished {false};
std::atomic<bool> restartRequested {false};
std::atomic<uint8_t> lastPublicationResult {
    static_cast<uint8_t>(RealtimePublicationResult::Accepted)
};
uint32_t nextPacedAt = 0U;
uint32_t nextServiceAt = 0U;
uint32_t restartAt = 0U;

uint32_t payloadChecksum(const HilPayload& payload) {
    return payload.revision ^ payload.value ^ payload.inverse ^ 0xA55A3CC3U;
}

void encode64(uint64_t value, uint8_t* output) {
    for (size_t i = 0U; i < 8U; ++i) {
        output[i] = static_cast<uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

void encode32(uint32_t value, uint8_t* output) {
    for (size_t i = 0U; i < 4U; ++i) {
        output[i] = static_cast<uint8_t>((value >> (8U * i)) & 0xFFU);
    }
}

bool invalidateResource(void*) {
    return snapshot.invalidate();
}

bool buildResource(void*, Identity::RuntimeIdentity identity,
                   RealtimeStreamPosition position) {
    if (failNextBuild.exchange(false)) return false;
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
    if (failNextNotification.exchange(false)) {
        lastPublicationResult.store(
            static_cast<uint8_t>(RealtimePublicationResult::Busy)
        );
        counters.busy.fetch_add(1U);
        return RealtimePublicationResult::Busy;
    }
    uint8_t frame[256U] {};
    frame[0] = 2U;
    encode64(metadata.runtimeIdentity().value(), &frame[1]);
    encode64(metadata.sequence().value(), &frame[9]);
    encode32(authoritative.revision, &frame[17]);
    encode32(authoritative.checksum, &frame[21]);
    for (size_t i = 25U; i < sizeof(frame); ++i) {
        frame[i] = static_cast<uint8_t>((authoritative.revision + i) & 0xFFU);
    }
    const RealtimePublicationResult result = transport.publishRealtime(
        metadata, RealtimeFrameType::Binary, frame, sizeof(frame)
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
    const RealtimeStreamPosition position = sequencer->currentPosition();
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
        "\"pacedRemaining\":%lu,\"lastPublicationResult\":%u,"
        "\"heapFreeInternal\":%u,\"heapMinInternal\":%u,"
        "\"heapLargestInternal\":%u}",
        static_cast<unsigned long long>(runtimeIdentity.value()),
        position.isBeforeFirst() ? "before-first" : "at",
        static_cast<unsigned long long>(position.hasSequence() ?
            position.sequence().value() : 0U),
        transport.isRealtimeRecoveryRequired() ? "true" : "false",
        publisher->isCoherentAt(runtimeIdentity, position) ? "true" : "false",
        static_cast<unsigned long>(authoritative.revision),
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
        static_cast<unsigned long>(pendingPaced.load()),
        static_cast<unsigned>(lastPublicationResult.load()),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)
    );
    writeJson(response, 200U, body);
}

void acceptedRoute(WebResponseWriter& response) {
    writeJson(response, 202U, "{\"accepted\":true}");
}

void changeRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    pendingChanges.fetch_add(1U);
    acceptedRoute(response);
}

void burstRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    pendingBurst.store(32U);
    acceptedRoute(response);
}

void blockedBurstRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    counters.firstBackpressureAttempt.store(0U);
    pendingBurst.store(32U);
    delay(750U); // HIL-only: holds HTTPD so four production work slots saturate.
    acceptedRoute(response);
}

void pacedRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    pendingPaced.store(600U);
    acceptedRoute(response);
}

void failSnapshotRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    failNextBuild.store(true);
    pendingChanges.fetch_add(1U);
    acceptedRoute(response);
}

void failNotificationRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    failNextNotification.store(true);
    pendingChanges.fetch_add(1U);
    acceptedRoute(response);
}

void recoveryRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    recoveryRequested.store(true);
    recoveryRepublished.store(false);
    acceptedRoute(response);
}

void restartRoute(void*, const HttpRouteRequest&, WebResponseWriter& response) {
    restartAt = millis() + 300U;
    restartRequested.store(true);
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
    const RealtimeRecoveryServiceResult result = transport.serviceRealtimeRecovery();
    switch (result) {
        case RealtimeRecoveryServiceResult::Idle:
            counters.serviceIdle.fetch_add(1U);
            break;
        case RealtimeRecoveryServiceResult::Progress:
            counters.serviceProgress.fetch_add(1U);
            break;
        case RealtimeRecoveryServiceResult::RetryNeeded:
            counters.serviceRetry.fetch_add(1U);
            break;
        case RealtimeRecoveryServiceResult::TransportRecycleSuggested:
            counters.serviceRecycle.fetch_add(1U);
            break;
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

    transport.addRoute("/api/hil/snapshot", HttpMethod::Get, snapshotRoute);
    transport.addRoute("/api/hil/status", HttpMethod::Get, statusRoute);
    transport.addRoute("/api/hil/change", HttpMethod::Post, changeRoute);
    transport.addRoute("/api/hil/burst", HttpMethod::Post, burstRoute);
    transport.addRoute("/api/hil/blocked-burst", HttpMethod::Post, blockedBurstRoute);
    transport.addRoute("/api/hil/paced", HttpMethod::Post, pacedRoute);
    transport.addRoute("/api/hil/fail-snapshot", HttpMethod::Post, failSnapshotRoute);
    transport.addRoute("/api/hil/fail-notification", HttpMethod::Post,
                       failNotificationRoute);
    transport.addRoute("/api/hil/recover", HttpMethod::Post, recoveryRoute);
    transport.addRoute("/api/hil/restart", HttpMethod::Post, restartRoute);
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
        !transport.begin(80U)) {
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
    if (static_cast<int32_t>(millis() - nextServiceAt) >= 0) {
        serviceTransport();
        nextServiceAt = millis() + 20U;
    }

    uint32_t changes = pendingChanges.exchange(0U);
    for (uint32_t i = 1U; i <= changes; ++i) applyOneTransition(i);

    const uint32_t burst = pendingBurst.exchange(0U);
    for (uint32_t i = 1U; i <= burst; ++i) {
        applyOneTransition(i);
        if (transport.isRealtimeRecoveryRequired()) break;
    }

    if (pendingPaced.load() != 0U &&
        static_cast<int32_t>(millis() - nextPacedAt) >= 0) {
        const uint32_t remaining = pendingPaced.fetch_sub(1U);
        if (remaining != 0U) applyOneTransition(601U - remaining);
        nextPacedAt = millis() + 5U;
    }
    if (transport.isRealtimeRecoveryRequired()) {
        pendingPaced.store(0U);
    }

    if (recoveryRequested.load()) {
        if (!recoveryRepublished.load()) {
            recoveryRepublished.store(
                publisher->republishCurrentCohort() ==
                    RealtimeCohortPublicationResult::Success
            );
        }
        if (recoveryRepublished.load() &&
            transport.tryClearRealtimeRecovery(
                runtimeIdentity, publisher->coherentPosition())) {
            counters.recoveryClears.fetch_add(1U);
            recoveryRequested.store(false);
        }
    }

    if (restartRequested.load() &&
        static_cast<int32_t>(millis() - restartAt) >= 0) {
        Serial.println("HIL_INTENTIONAL_RESTART");
        Serial.flush();
        ESP.restart();
    }
    delay(1U);
}
