#include <unity.h>

#include "AquaCore/Web/PublishedSnapshot.h"
#include "AquaCore/Web/RealtimeResync.h"
#include "AquaCore/Web/RealtimeStreamStartWire.h"

using namespace AquaCore;
using namespace AquaCore::Web;

namespace AquaCore { namespace Web {
class RealtimeStreamSequencerTestAccess {
public:
    static void setLast(RealtimeStreamSequencer& sequencer, uint64_t value) {
        sequencer.last_ = value;
    }
};
} }

namespace {

Identity::RuntimeIdentity runtime(uint64_t value);
RealtimeStreamPosition position(uint64_t value);

void assertStreamStart(uint64_t sequence, const char* expected) {
    RealtimeStreamStartState state;
    TEST_ASSERT_TRUE(RealtimeStreamStartState::fromValues(
        runtime(UINT64_C(0x0123456789ABCDEF)), position(sequence), state));
    uint8_t output[STREAM_START_WIRE_CAPACITY] {};
    size_t length = 99U;
    TEST_ASSERT_TRUE(encodeRealtimeStreamStart(state, output, sizeof(output), length));
    TEST_ASSERT_EQUAL_STRING(expected, reinterpret_cast<const char*>(output));
    TEST_ASSERT_EQUAL_UINT32(strlen(expected), length);
    TEST_ASSERT_EQUAL_UINT8(0U, output[length]);
    if (sequence == UINT64_MAX) {
        TEST_ASSERT_EQUAL_UINT32(STREAM_START_WIRE_MAX_LENGTH, length);
    }

    uint8_t exact[STREAM_START_WIRE_CAPACITY] {};
    size_t exactLength = 99U;
    TEST_ASSERT_TRUE(encodeRealtimeStreamStart(state, exact, length + 1U, exactLength));
    TEST_ASSERT_EQUAL_UINT32(length, exactLength);
    uint8_t shortOutput[STREAM_START_WIRE_CAPACITY];
    memset(shortOutput, 0xA5, sizeof(shortOutput));
    size_t shortLength = 99U;
    TEST_ASSERT_FALSE(encodeRealtimeStreamStart(state, shortOutput, length, shortLength));
    TEST_ASSERT_EQUAL_UINT32(0U, shortLength);
    for (size_t i = 0U; i < sizeof(shortOutput); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0xA5U, shortOutput[i]);
    }
}

void test_stream_start_wire_exact_frames_and_bounds() {
    RealtimeStreamStartState baseline;
    TEST_ASSERT_TRUE(RealtimeStreamStartState::fromValues(
        runtime(UINT64_C(0x0123456789ABCDEF)),
        RealtimeStreamPosition::beforeFirst(), baseline));
    uint8_t output[STREAM_START_WIRE_CAPACITY] {};
    size_t length = 99U;
    TEST_ASSERT_TRUE(encodeRealtimeStreamStart(baseline, output, sizeof(output), length));
    TEST_ASSERT_EQUAL_STRING(
        "{\"type\":\"stream_start\",\"runtime_id\":\"0123456789ABCDEF\",\"position\":{\"kind\":\"before_first\"}}",
        reinterpret_cast<const char*>(output));
    TEST_ASSERT_EQUAL_UINT32(90U, length);
    TEST_ASSERT_EQUAL_UINT8(0U, output[length]);
    size_t exactLength = 99U;
    TEST_ASSERT_TRUE(encodeRealtimeStreamStart(baseline, output, length + 1U, exactLength));
    TEST_ASSERT_EQUAL_UINT32(length, exactLength);
    size_t shortLength = 99U;
    TEST_ASSERT_FALSE(encodeRealtimeStreamStart(baseline, output, length, shortLength));
    TEST_ASSERT_EQUAL_UINT32(0U, shortLength);

    assertStreamStart(1U,
        "{\"type\":\"stream_start\",\"runtime_id\":\"0123456789ABCDEF\",\"position\":{\"kind\":\"at\",\"sequence\":\"1\"}}");
    assertStreamStart(10U,
        "{\"type\":\"stream_start\",\"runtime_id\":\"0123456789ABCDEF\",\"position\":{\"kind\":\"at\",\"sequence\":\"10\"}}");
    assertStreamStart(UINT64_MAX,
        "{\"type\":\"stream_start\",\"runtime_id\":\"0123456789ABCDEF\",\"position\":{\"kind\":\"at\",\"sequence\":\"18446744073709551615\"}}");
}

void test_stream_start_wire_invalid_inputs_leave_storage_unchanged() {
    RealtimeStreamStartState state;
    uint8_t output[STREAM_START_WIRE_CAPACITY];
    memset(output, 0xA5, sizeof(output));
    size_t length = 99U;
    TEST_ASSERT_FALSE(encodeRealtimeStreamStart(state, output, sizeof(output), length));
    TEST_ASSERT_EQUAL_UINT32(0U, length);
    TEST_ASSERT_FALSE(RealtimeStreamStartState::fromValues(
        Identity::RuntimeIdentity(), RealtimeStreamPosition::beforeFirst(), state));
    TEST_ASSERT_FALSE(RealtimeStreamStartState::fromValues(
        runtime(1U), RealtimeStreamPosition(), state));
    TEST_ASSERT_FALSE(encodeRealtimeStreamStart(state, output, sizeof(output), length));
    TEST_ASSERT_EQUAL_UINT8(0xA5U, output[0]);
    TEST_ASSERT_TRUE(RealtimeStreamStartState::fromValues(
        runtime(1U), RealtimeStreamPosition::beforeFirst(), state));
    length = 99U;
    TEST_ASSERT_FALSE(encodeRealtimeStreamStart(state, nullptr, sizeof(output), length));
    TEST_ASSERT_EQUAL_UINT32(0U, length);
}

Identity::RuntimeIdentity runtime(uint64_t value) {
    Identity::RuntimeIdentity output;
    Identity::RuntimeIdentity::fromValue(value, output);
    return output;
}

RealtimeStreamPosition position(uint64_t value) {
    RealtimeStreamSequence sequence;
    RealtimeStreamSequence::fromValue(value, sequence);
    RealtimeStreamPosition output;
    RealtimeStreamPosition::at(sequence, output);
    return output;
}

} // namespace

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include "AquaCore/Web/Esp32SnapshotSynchronizer.h"
#include "AquaCore/Web/EspIdfWebTransport.h"

namespace {
struct ProofPayload { uint32_t value; };
bool proofInvalidate(void*) { return true; }
bool proofBuild(void*, Identity::RuntimeIdentity, RealtimeStreamPosition) { return true; }
bool proofPublish(void*) { return true; }
bool proofState(void* context, const RealtimeStreamStartState& state) {
    return static_cast<EspIdfWebTransport*>(context)
        ->publishRealtimeStreamStartState(state);
}
RealtimePublicationResult proofNotify(void*, const RealtimeNotificationMetadata&) {
    return RealtimePublicationResult::Accepted;
}
void proofRecovery(void* context) {
    static_cast<EspIdfWebTransport*>(context)->requestRealtimeRecovery();
}
}

volatile bool startTransportProof = false;
void setup() {
    UNITY_BEGIN();
    Esp32SnapshotSynchronizer synchronizer;
    PublishedSnapshot<RealtimeSnapshot<ProofPayload> > snapshot(synchronizer);
    EspIdfWebTransport transport;
    RealtimeStreamSequencer sequencer(runtime(91U));
    RealtimeResyncResourceBinding binding {
        nullptr, proofInvalidate, proofBuild, proofPublish
    };
    RealtimeCohortPublisher<1U> publisher(
        runtime(91U), sequencer, &binding, 1U,
        proofState, &transport, proofNotify, nullptr,
        proofRecovery, &transport
    );
    (void)publisher.publishBaseline();
    transport.requestRealtimeRecovery();
    (void)transport.serviceRealtimeRecovery();
    if (startTransportProof) transport.begin(80U);
    transport.stop();
    UNITY_END();
}
void loop() {}

#else

#include <mutex>

namespace {

class NativeSynchronizer final : public SnapshotSynchronizer {
public:
    bool lock() override { mutex_.lock(); return true; }
    void unlock() override { mutex_.unlock(); }
private:
    std::mutex mutex_;
};

struct SystemPayload { uint32_t value; };
struct DomainPayload { uint16_t mode; uint16_t count; };
static_assert(std::is_trivially_copyable<RealtimeSnapshot<SystemPayload> >::value,
              "snapshot wrapper must remain trivially copyable");

struct Trace {
    uint8_t values[64U] {};
    size_t count = 0U;
    void add(uint8_t value) { if (count < 64U) values[count++] = value; }
};

template <typename Payload>
struct ResourceFixture {
    ResourceFixture(SnapshotSynchronizer& sync, Trace& traceValue,
                    uint8_t buildValue, uint8_t publishValue)
        : publication(sync), trace(traceValue), buildCode(buildValue),
          publishCode(publishValue) {}

    PublishedSnapshot<RealtimeSnapshot<Payload> > publication;
    RealtimeSnapshot<Payload> candidate;
    Payload payload {};
    Trace& trace;
    uint8_t buildCode;
    uint8_t publishCode;
    bool failBuild = false;
    bool failPublish = false;
    bool failInvalidate = false;
};

template <typename Payload>
bool invalidateResource(void* context) {
    ResourceFixture<Payload>& fixture =
        *static_cast<ResourceFixture<Payload>*>(context);
    if (fixture.failInvalidate) return false;
    return fixture.publication.invalidate();
}

template <typename Payload>
bool buildResource(void* context, Identity::RuntimeIdentity identity,
                   RealtimeStreamPosition watermark) {
    ResourceFixture<Payload>& fixture =
        *static_cast<ResourceFixture<Payload>*>(context);
    fixture.trace.add(fixture.buildCode);
    if (fixture.failBuild) return false;
    return RealtimeSnapshot<Payload>::coherent(
        identity, watermark, fixture.payload, fixture.candidate
    );
}

template <typename Payload>
bool publishResource(void* context) {
    ResourceFixture<Payload>& fixture =
        *static_cast<ResourceFixture<Payload>*>(context);
    fixture.trace.add(fixture.publishCode);
    return !fixture.failPublish && fixture.publication.publish(fixture.candidate);
}

struct CoordinatorFixture {
    Trace* trace = nullptr;
    RealtimeStreamStartState state;
    bool failState = false;
    RealtimePublicationResult notificationResult = RealtimePublicationResult::Accepted;
    unsigned notifications = 0U;
    unsigned recoveries = 0U;
};

bool publishState(void* context, const RealtimeStreamStartState& state) {
    CoordinatorFixture& fixture = *static_cast<CoordinatorFixture*>(context);
    fixture.trace->add(5U);
    if (fixture.failState) return false;
    fixture.state = state;
    return true;
}

RealtimePublicationResult submitNotification(
    void* context, const RealtimeNotificationMetadata&
) {
    CoordinatorFixture& fixture = *static_cast<CoordinatorFixture*>(context);
    fixture.trace->add(6U);
    ++fixture.notifications;
    return fixture.notificationResult;
}

void requestRecovery(void* context) {
    CoordinatorFixture& fixture = *static_cast<CoordinatorFixture*>(context);
    fixture.trace->add(7U);
    ++fixture.recoveries;
}

struct CohortFixture {
    NativeSynchronizer synchronizer;
    Trace trace;
    ResourceFixture<SystemPayload> system;
    ResourceFixture<DomainPayload> domain;
    RealtimeResyncResourceBinding resources[2U];
    CoordinatorFixture callbacks;
    RealtimeStreamSequencer sequencer;
    RealtimeCohortPublisher<2U> publisher;

    CohortFixture()
        : system(synchronizer, trace, 1U, 3U),
          domain(synchronizer, trace, 2U, 4U),
          resources {
              {&system, invalidateResource<SystemPayload>,
               buildResource<SystemPayload>, publishResource<SystemPayload>},
              {&domain, invalidateResource<DomainPayload>,
               buildResource<DomainPayload>, publishResource<DomainPayload>}
          }, callbacks(), sequencer(runtime(44U)),
          publisher(runtime(44U), sequencer, resources, 2U,
                    publishState, &callbacks, submitNotification, &callbacks,
                    requestRecovery, &callbacks) {
        callbacks.trace = &trace;
        system.payload.value = 10U;
        domain.payload.mode = 2U;
        domain.payload.count = 7U;
    }
};

void test_position_and_current_position() {
    const RealtimeStreamPosition before = RealtimeStreamPosition::beforeFirst();
    TEST_ASSERT_TRUE(before.isValid());
    TEST_ASSERT_TRUE(before.isBeforeFirst());
    RealtimeStreamSequence invalid;
    RealtimeStreamPosition output;
    TEST_ASSERT_FALSE(RealtimeStreamPosition::at(invalid, output));
    TEST_ASSERT_FALSE(output.isValid());
    int ordering = 99;
    TEST_ASSERT_FALSE(output.compare(before, ordering));
    TEST_ASSERT_EQUAL_INT(0, ordering);
    TEST_ASSERT_TRUE(before.compare(position(1U), ordering));
    TEST_ASSERT_TRUE(ordering < 0);
    TEST_ASSERT_TRUE(position(1U).compare(position(2U), ordering));
    TEST_ASSERT_TRUE(ordering < 0);
    TEST_ASSERT_TRUE(position(2U) == position(2U));

    RealtimeStreamSequencer sequencer(runtime(1U));
    TEST_ASSERT_TRUE(sequencer.currentPosition().isBeforeFirst());
    RealtimeNotificationMetadata metadata;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeSequenceIssueResult::Success),
        static_cast<uint8_t>(sequencer.issue(metadata))
    );
    TEST_ASSERT_TRUE(sequencer.currentPosition() == position(1U));
    RealtimeStreamSequencerTestAccess::setLast(sequencer, UINT64_MAX - 1U);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeSequenceIssueResult::Success),
        static_cast<uint8_t>(sequencer.issue(metadata))
    );
    TEST_ASSERT_TRUE(sequencer.currentPosition() == position(UINT64_MAX));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeSequenceIssueResult::Exhausted),
        static_cast<uint8_t>(sequencer.issue(metadata))
    );
    TEST_ASSERT_TRUE(sequencer.currentPosition() == position(UINT64_MAX));
}

void test_snapshot_wrapper_is_one_independent_copy() {
    NativeSynchronizer synchronizer;
    PublishedSnapshot<RealtimeSnapshot<SystemPayload> > publication(synchronizer);
    RealtimeSnapshot<SystemPayload> wrapped;
    SystemPayload payload {12U};
    TEST_ASSERT_TRUE(RealtimeSnapshot<SystemPayload>::coherent(
        runtime(2U), RealtimeStreamPosition::beforeFirst(), payload, wrapped
    ));
    TEST_ASSERT_TRUE(wrapped.isCoherentCurrent());
    TEST_ASSERT_TRUE(publication.publish(wrapped));
    payload.value = 99U;
    RealtimeSnapshot<SystemPayload> copy;
    TEST_ASSERT_TRUE(publication.read(copy));
    TEST_ASSERT_EQUAL_UINT32(12U, copy.payload().value);
    TEST_ASSERT_TRUE(copy.watermark().isBeforeFirst());
    TEST_ASSERT_FALSE(RealtimeSnapshot<SystemPayload>::coherent(
        Identity::RuntimeIdentity(), position(1U), payload, wrapped
    ));

    // An outer publication alone is not proof of a coherent-current value.
    RealtimeSnapshot<SystemPayload> unavailable;
    TEST_ASSERT_TRUE(publication.publish(unavailable));
    TEST_ASSERT_TRUE(publication.read(copy));
    TEST_ASSERT_FALSE(copy.isCoherentCurrent());
}

void test_baseline_and_common_watermark_for_unchanged_resource() {
    CohortFixture fixture;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::Success),
        static_cast<uint8_t>(fixture.publisher.publishBaseline())
    );
    RealtimeSnapshot<SystemPayload> system;
    RealtimeSnapshot<DomainPayload> domain;
    TEST_ASSERT_TRUE(fixture.system.publication.read(system));
    TEST_ASSERT_TRUE(fixture.domain.publication.read(domain));
    TEST_ASSERT_TRUE(system.watermark().isBeforeFirst());
    TEST_ASSERT_TRUE(domain.watermark().isBeforeFirst());
    TEST_ASSERT_TRUE(fixture.sequencer.currentPosition().isBeforeFirst());

    fixture.trace.count = 0U;
    fixture.system.payload.value = 11U;
    const uint16_t unchangedMode = fixture.domain.payload.mode;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::Success),
        static_cast<uint8_t>(fixture.publisher.publishTransition())
    );
    TEST_ASSERT_TRUE(fixture.system.publication.read(system));
    TEST_ASSERT_TRUE(fixture.domain.publication.read(domain));
    TEST_ASSERT_TRUE(system.watermark() == position(1U));
    TEST_ASSERT_TRUE(domain.watermark() == position(1U));
    TEST_ASSERT_EQUAL_UINT16(unchangedMode, domain.payload().mode);
    TEST_ASSERT_TRUE(fixture.callbacks.state.position() == position(1U));
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.callbacks.notifications);
}

void test_exact_publication_order() {
    CohortFixture fixture;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::Success),
        static_cast<uint8_t>(fixture.publisher.publishTransition())
    );
    TEST_ASSERT_EQUAL_UINT32(6U, fixture.trace.count);
    const uint8_t expected[] {1U, 2U, 3U, 4U, 5U, 6U};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, fixture.trace.values, 6U);
}

void test_failure_withholds_notification_and_republish_uses_same_position() {
    CohortFixture fixture;
    fixture.domain.failPublish = true;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::ResourceFailure),
        static_cast<uint8_t>(fixture.publisher.publishTransition())
    );
    TEST_ASSERT_TRUE(fixture.sequencer.currentPosition() == position(1U));
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.callbacks.notifications);
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.callbacks.recoveries);
    TEST_ASSERT_FALSE(fixture.publisher.isCoherentAt(runtime(44U), position(1U)));

    fixture.domain.failPublish = false;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::Success),
        static_cast<uint8_t>(fixture.publisher.republishCurrentCohort())
    );
    TEST_ASSERT_TRUE(fixture.sequencer.currentPosition() == position(1U));
    TEST_ASSERT_TRUE(fixture.publisher.isCoherentAt(runtime(44U), position(1U)));
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.callbacks.notifications);

    fixture.system.payload.value = 13U;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::Success),
        static_cast<uint8_t>(fixture.publisher.publishTransition())
    );
    TEST_ASSERT_TRUE(fixture.publisher.isCoherentAt(runtime(44U), position(2U)));
}

void test_notification_failure_keeps_snapshot_and_requests_recovery() {
    const RealtimePublicationResult failures[] {
        RealtimePublicationResult::NotRunning,
        RealtimePublicationResult::RecoveryRequired,
        RealtimePublicationResult::InvalidMetadata,
        RealtimePublicationResult::PayloadTooLarge,
        RealtimePublicationResult::Busy,
        RealtimePublicationResult::QueueWorkFailure
    };
    for (size_t i = 0U; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        CohortFixture fixture;
        fixture.callbacks.notificationResult = failures[i];
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(RealtimeCohortPublicationResult::NotificationFailure),
            static_cast<uint8_t>(fixture.publisher.publishTransition())
        );
        TEST_ASSERT_TRUE(fixture.publisher.isCoherentAt(runtime(44U), position(1U)));
        TEST_ASSERT_TRUE(fixture.callbacks.state.position() == position(1U));
        TEST_ASSERT_EQUAL_UINT32(1U, fixture.callbacks.recoveries);
    }
}

void test_stream_start_state_failure_spends_position_and_withholds_notification() {
    CohortFixture fixture;
    fixture.callbacks.failState = true;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::StreamStateFailure),
        static_cast<uint8_t>(fixture.publisher.publishTransition())
    );
    TEST_ASSERT_TRUE(fixture.sequencer.currentPosition() == position(1U));
    TEST_ASSERT_EQUAL_UINT32(0U, fixture.callbacks.notifications);
    TEST_ASSERT_EQUAL_UINT32(1U, fixture.callbacks.recoveries);
    TEST_ASSERT_FALSE(fixture.publisher.isCoherentAt(runtime(44U), position(1U)));
}

void test_invalid_and_duplicate_compositions_fail_closed() {
    CohortFixture duplicate;
    duplicate.resources[1] = duplicate.resources[0];
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::InvalidComposition),
        static_cast<uint8_t>(duplicate.publisher.publishBaseline())
    );
    TEST_ASSERT_EQUAL_UINT32(1U, duplicate.callbacks.recoveries);

    NativeSynchronizer synchronizer;
    Trace trace;
    ResourceFixture<SystemPayload> resource(synchronizer, trace, 1U, 2U);
    RealtimeResyncResourceBinding binding {
        &resource, invalidateResource<SystemPayload>,
        buildResource<SystemPayload>, publishResource<SystemPayload>
    };
    CoordinatorFixture callbacks;
    callbacks.trace = &trace;
    RealtimeStreamSequencer sequencer(runtime(55U));
    RealtimeCohortPublisher<1U> empty(
        runtime(55U), sequencer, &binding, 0U, publishState, &callbacks,
        submitNotification, &callbacks, requestRecovery, &callbacks
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::InvalidComposition),
        static_cast<uint8_t>(empty.publishBaseline())
    );
    RealtimeCohortPublisher<1U> overflow(
        runtime(55U), sequencer, &binding, 2U, publishState, &callbacks,
        submitNotification, &callbacks, requestRecovery, &callbacks
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeCohortPublicationResult::InvalidComposition),
        static_cast<uint8_t>(overflow.publishBaseline())
    );
}

void test_client_gate_connecting_gap_and_generation() {
    RealtimeClientRegistry<2U> clients;
    RealtimeClientToken first;
    TEST_ASSERT_TRUE(clients.connect(4, position(1U), first));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeClientState::Connecting),
                            static_cast<uint8_t>(clients.slot(first.slot)->state));
    clients.notePublication(position(2U));
    TEST_ASSERT_FALSE(clients.markerSucceeded(first, false));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeClientState::Closing),
                            static_cast<uint8_t>(clients.slot(first.slot)->state));
    clients.release(first);

    RealtimeClientToken second;
    TEST_ASSERT_TRUE(clients.connect(4, position(2U), second));
    TEST_ASSERT_TRUE(second.generation != first.generation);
    clients.markerFailed(first);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeClientState::Connecting),
                            static_cast<uint8_t>(clients.slot(second.slot)->state));
    TEST_ASSERT_TRUE(clients.markerSucceeded(second, false));
    clients.sendFailed(second);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeClientState::Closing),
                            static_cast<uint8_t>(clients.slot(second.slot)->state));
    RealtimeRecoveryState recovery;
    TEST_ASSERT_FALSE(recovery.isRequired());
    clients.release(second);
    RealtimeClientToken third;
    TEST_ASSERT_TRUE(clients.connect(5, position(2U), third));
    TEST_ASSERT_FALSE(clients.markerSucceeded(third, true));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeClientState::Closing),
                            static_cast<uint8_t>(clients.slot(third.slot)->state));
}

void test_connecting_boundary_and_bounded_client_cleanup() {
    RealtimeClientRegistry<1U> clients;
    RealtimeClientToken token;
    TEST_ASSERT_TRUE(clients.connect(7, position(2U), token));
    clients.notePublication(position(1U));
    clients.notePublication(position(2U));
    TEST_ASSERT_FALSE(clients.slot(token.slot)->missedWhileConnecting);
    clients.notePublication(position(3U));
    TEST_ASSERT_TRUE(clients.slot(token.slot)->missedWhileConnecting);
    TEST_ASSERT_FALSE(clients.markerSucceeded(token, false));

    TEST_ASSERT_TRUE(clients.markCloseRequested(token, 7));
    TEST_ASSERT_FALSE(clients.markCloseRequested(token, 7));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeClientState::Closing),
                            static_cast<uint8_t>(clients.slot(token.slot)->state));
    TEST_ASSERT_TRUE(clients.slot(token.slot)->closeRequested);
    TEST_ASSERT_FALSE(clients.reclaimInactive(token, 8));
    TEST_ASSERT_TRUE(clients.reclaimInactive(token, 7));
    TEST_ASSERT_FALSE(clients.hasClients());
}

void test_single_client_closing_retry_preserves_peer_and_generation() {
    RealtimeStreamSequencer sequencer(runtime(73U));
    RealtimeNotificationMetadata notification;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimeSequenceIssueResult::Success),
        static_cast<uint8_t>(sequencer.issue(notification)));
    const RealtimeStreamPosition atOne = sequencer.currentPosition();

    RealtimeClientRegistry<2U> clients;
    RealtimeClientToken failing, healthy;
    TEST_ASSERT_TRUE(clients.connect(7, atOne, failing));
    TEST_ASSERT_TRUE(clients.markerSucceeded(failing, false));
    TEST_ASSERT_TRUE(clients.connect(8, atOne, healthy));
    TEST_ASSERT_TRUE(clients.markerSucceeded(healthy, false));

    clients.sendFailed(failing);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeClientState::Closing),
                            static_cast<uint8_t>(clients.slot(failing.slot)->state));
    // Two failed dependency calls must leave the exact token available to
    // serviceRealtimeRecovery; only a successful close marks it requested.
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        TEST_ASSERT_FALSE(clients.slot(failing.slot)->closeRequested);
        TEST_ASSERT_TRUE(clients.isTokenCurrentForFd(failing, 7));
    }
    TEST_ASSERT_TRUE(clients.markCloseRequested(failing, 7));
    TEST_ASSERT_TRUE(clients.slot(failing.slot)->closeRequested);
    TEST_ASSERT_TRUE(clients.reclaimInactive(failing, 7));
    TEST_ASSERT_FALSE(clients.isTokenCurrent(failing));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeClientState::Live),
                            static_cast<uint8_t>(clients.slot(healthy.slot)->state));

    RealtimeClientToken replacement;
    TEST_ASSERT_TRUE(clients.connect(7, atOne, replacement));
    TEST_ASSERT_TRUE(replacement.generation > failing.generation);
    TEST_ASSERT_TRUE(clients.markerSucceeded(replacement, false));
    TEST_ASSERT_FALSE(clients.reclaimInactive(failing, 7));
    TEST_ASSERT_FALSE(clients.markCloseRequested(failing, 7));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeClientState::Live),
                            static_cast<uint8_t>(clients.slot(replacement.slot)->state));
    RealtimeRecoveryState recovery;
    TEST_ASSERT_FALSE(recovery.isRequired());
    TEST_ASSERT_TRUE(sequencer.currentPosition() == atOne);
}

void test_recovery_state_is_sticky_and_clear_is_position_checked() {
    RealtimeRecoveryState recovery;
    RealtimeStreamStartState state;
    RealtimeStreamStartState::fromValues(runtime(8U), position(3U), state);
    TEST_ASSERT_TRUE(recovery.allowsFanout());
    const RealtimeRecoveryState::Generation oldGeneration = recovery.generation();
    TEST_ASSERT_TRUE(recovery.acceptsGeneration(oldGeneration));
    recovery.request();
    recovery.request();
    TEST_ASSERT_TRUE(recovery.isRequired());
    const RealtimeRecoveryState::Generation recoveryGeneration = recovery.generation();
    TEST_ASSERT_TRUE(recoveryGeneration != oldGeneration);
    TEST_ASSERT_FALSE(recovery.tryClear(false, true, state, runtime(8U), position(3U)));
    TEST_ASSERT_FALSE(recovery.tryClear(true, false, state, runtime(8U), position(3U)));
    TEST_ASSERT_FALSE(recovery.tryClear(true, true, state, runtime(8U), position(2U)));
    TEST_ASSERT_TRUE(recovery.isRequired());
    TEST_ASSERT_TRUE(recovery.tryClear(true, true, state, runtime(8U), position(3U)));
    TEST_ASSERT_FALSE(recovery.isRequired());
    TEST_ASSERT_FALSE(recovery.acceptsGeneration(oldGeneration));
    TEST_ASSERT_TRUE(recovery.acceptsGeneration(recoveryGeneration));
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_position_and_current_position);
    RUN_TEST(test_snapshot_wrapper_is_one_independent_copy);
    RUN_TEST(test_baseline_and_common_watermark_for_unchanged_resource);
    RUN_TEST(test_exact_publication_order);
    RUN_TEST(test_failure_withholds_notification_and_republish_uses_same_position);
    RUN_TEST(test_notification_failure_keeps_snapshot_and_requests_recovery);
    RUN_TEST(test_stream_start_state_failure_spends_position_and_withholds_notification);
    RUN_TEST(test_invalid_and_duplicate_compositions_fail_closed);
    RUN_TEST(test_client_gate_connecting_gap_and_generation);
    RUN_TEST(test_connecting_boundary_and_bounded_client_cleanup);
    RUN_TEST(test_single_client_closing_retry_preserves_peer_and_generation);
    RUN_TEST(test_recovery_state_is_sticky_and_clear_is_position_checked);
    RUN_TEST(test_stream_start_wire_exact_frames_and_bounds);
    RUN_TEST(test_stream_start_wire_invalid_inputs_leave_storage_unchanged);
    return UNITY_END();
}

#endif
