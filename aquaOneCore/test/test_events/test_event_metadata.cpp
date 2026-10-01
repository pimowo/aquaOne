#include <stdint.h>
#include <type_traits>
#include <unity.h>

#include "AquaCore/Events/EventMetadata.h"
#include "AquaCore/Events/EventSink.h"

namespace AquaCore {
namespace Events {

// The test-only friend has no production constructor or reset API.
class EventMetadataSequencerTestAccess {
public:
    static void seedLastIssued(EventMetadataSequencer& sequencer, uint64_t value) {
        sequencer.lastIssued_ = value;
    }
};

} // namespace Events
} // namespace AquaCore

namespace {

namespace Events = AquaCore::Events;
namespace Identity = AquaCore::Identity;

static_assert(!std::is_copy_constructible<Events::EventMetadataSequencer>::value,
    "A stream owner must not be copied");

Identity::RuntimeIdentity runtimeFrom(uint64_t value) {
    Identity::RuntimeIdentity result;
    TEST_ASSERT_TRUE(Identity::RuntimeIdentity::fromValue(value, result));
    return result;
}

Events::EventSequence sequenceFrom(uint64_t value) {
    Events::EventSequence result;
    TEST_ASSERT_TRUE(Events::EventSequence::fromValue(value, result));
    return result;
}

void testSequenceDefaultAndZeroAreInvalid() {
    Events::EventSequence sequence;
    TEST_ASSERT_FALSE(sequence.isValid());
    TEST_ASSERT_EQUAL_UINT64(0U, sequence.value());
    TEST_ASSERT_FALSE(Events::EventSequence::fromValue(0U, sequence));
    TEST_ASSERT_FALSE(sequence.isValid());
}

void testSequenceEndpointsAndFullWidthEquality() {
    const Events::EventSequence one = sequenceFrom(1U);
    const Events::EventSequence maximum = sequenceFrom(UINT64_MAX);
    const Events::EventSequence high = sequenceFrom(UINT64_C(0x100000001));
    const Events::EventSequence low = sequenceFrom(UINT64_C(0x200000001));
    TEST_ASSERT_TRUE(one.isValid());
    TEST_ASSERT_TRUE(maximum.isValid());
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, maximum.value());
    TEST_ASSERT_TRUE(high == sequenceFrom(UINT64_C(0x100000001)));
    TEST_ASSERT_FALSE(high == low);
    TEST_ASSERT_TRUE(high != low);
}

void testValidMetadataPreservesBothFullWidthValues() {
    const Identity::RuntimeIdentity runtime = runtimeFrom(UINT64_C(0xFEDCBA9800000001));
    const Events::EventSequence sequence = sequenceFrom(UINT64_C(0x1234567800000002));
    Events::EventMetadata metadata;
    TEST_ASSERT_TRUE(Events::EventMetadata::fromValues(runtime, sequence, metadata));
    TEST_ASSERT_TRUE(metadata.isValid());
    TEST_ASSERT_EQUAL_UINT64(runtime.value(), metadata.runtimeIdentity().value());
    TEST_ASSERT_EQUAL_UINT64(sequence.value(), metadata.sequence().value());
}

void testMetadataRejectsInvalidRuntimeAndClearsOutput() {
    Events::EventMetadata metadata;
    TEST_ASSERT_TRUE(Events::EventMetadata::fromValues(
        runtimeFrom(5U), sequenceFrom(8U), metadata));
    TEST_ASSERT_FALSE(Events::EventMetadata::fromValues(
        Identity::RuntimeIdentity(), sequenceFrom(1U), metadata));
    TEST_ASSERT_FALSE(metadata.isValid());
    TEST_ASSERT_FALSE(metadata.runtimeIdentity().isValid());
    TEST_ASSERT_FALSE(metadata.sequence().isValid());
}

void testMetadataRejectsInvalidSequenceAndClearsOutput() {
    Events::EventMetadata metadata;
    TEST_ASSERT_TRUE(Events::EventMetadata::fromValues(
        runtimeFrom(5U), sequenceFrom(8U), metadata));
    TEST_ASSERT_FALSE(Events::EventMetadata::fromValues(
        runtimeFrom(5U), Events::EventSequence(), metadata));
    TEST_ASSERT_FALSE(metadata.isValid());
    TEST_ASSERT_FALSE(metadata.sequence().isValid());
}

void testSequencerStartsAtOneAndIncrementsForOneRuntime() {
    const Identity::RuntimeIdentity runtime = runtimeFrom(UINT64_C(0xAABBCCDD00000001));
    Events::EventMetadataSequencer sequencer(runtime);
    Events::EventMetadata metadata;
    for (uint64_t expected = 1U; expected <= 3U; ++expected) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(Events::EventIssueResult::Success),
            static_cast<int>(sequencer.issue(metadata)));
        TEST_ASSERT_TRUE(metadata.isValid());
        TEST_ASSERT_EQUAL_UINT64(expected, metadata.sequence().value());
        TEST_ASSERT_EQUAL_UINT64(runtime.value(), metadata.runtimeIdentity().value());
    }
    TEST_ASSERT_FALSE(sequencer.isExhausted());
}

void testNewStreamStartsAtOneAndBindsCopiedRuntime() {
    Identity::RuntimeIdentity firstRuntime = runtimeFrom(42U);
    const Identity::RuntimeIdentity secondRuntime = runtimeFrom(43U);
    Events::EventMetadataSequencer first(firstRuntime);
    Events::EventMetadataSequencer second(secondRuntime);
    firstRuntime = runtimeFrom(99U);
    Events::EventMetadata firstMetadata;
    Events::EventMetadata secondMetadata;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Events::EventIssueResult::Success),
        static_cast<int>(first.issue(firstMetadata)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Events::EventIssueResult::Success),
        static_cast<int>(second.issue(secondMetadata)));
    TEST_ASSERT_EQUAL_UINT64(1U, firstMetadata.sequence().value());
    TEST_ASSERT_EQUAL_UINT64(1U, secondMetadata.sequence().value());
    TEST_ASSERT_EQUAL_UINT64(42U, firstMetadata.runtimeIdentity().value());
    TEST_ASSERT_EQUAL_UINT64(43U, secondMetadata.runtimeIdentity().value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Events::EventIssueResult::Success),
        static_cast<int>(first.issue(firstMetadata)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Events::EventIssueResult::Success),
        static_cast<int>(second.issue(secondMetadata)));
    TEST_ASSERT_EQUAL_UINT64(2U, firstMetadata.sequence().value());
    TEST_ASSERT_EQUAL_UINT64(2U, secondMetadata.sequence().value());
}

void testInvalidRuntimeNeverIssuesOrAdvances() {
    Events::EventMetadataSequencer invalid{Identity::RuntimeIdentity()};
    Events::EventMetadata metadata;
    TEST_ASSERT_TRUE(Events::EventMetadata::fromValues(
        runtimeFrom(1U), sequenceFrom(1U), metadata));
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(Events::EventIssueResult::InvalidRuntimeIdentity),
            static_cast<int>(invalid.issue(metadata)));
        TEST_ASSERT_FALSE(metadata.isValid());
        TEST_ASSERT_EQUAL_UINT64(0U, metadata.sequence().value());
    }
    TEST_ASSERT_FALSE(invalid.isExhausted());
}

void testMaximumIssuesOnceThenExhaustionIsPermanent() {
    Events::EventMetadataSequencer sequencer(runtimeFrom(100U));
    Events::EventMetadataSequencerTestAccess::seedLastIssued(sequencer, UINT64_MAX - 2U);
    Events::EventMetadata metadata;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Events::EventIssueResult::Success),
        static_cast<int>(sequencer.issue(metadata)));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 1U, metadata.sequence().value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Events::EventIssueResult::Success),
        static_cast<int>(sequencer.issue(metadata)));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, metadata.sequence().value());
    const Events::EventMetadata finalLegalMetadata = metadata;
    TEST_ASSERT_TRUE(sequencer.isExhausted());
    for (unsigned attempt = 0U; attempt < 2U; ++attempt) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(Events::EventIssueResult::Exhausted),
            static_cast<int>(sequencer.issue(metadata)));
        TEST_ASSERT_FALSE(metadata.isValid());
        TEST_ASSERT_EQUAL_UINT64(0U, metadata.sequence().value());
    }
    TEST_ASSERT_TRUE(finalLegalMetadata.isValid());
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, finalLegalMetadata.sequence().value());
}

void testSemanticEventSinkDoesNotReceiveMetadata() {
    struct DomainEvent { int value; };
    class Sink final : public Events::EventSink<DomainEvent> {
    public:
        void emit(const DomainEvent& event) override { observed = event.value; }
        int observed = 0;
    } sink;
    Events::EventSink<DomainEvent>& boundary = sink;
    boundary.emit(DomainEvent{12});
    TEST_ASSERT_EQUAL_INT(12, sink.observed);
}

} // namespace

void runEventMetadataTests() {
    RUN_TEST(testSequenceDefaultAndZeroAreInvalid);
    RUN_TEST(testSequenceEndpointsAndFullWidthEquality);
    RUN_TEST(testValidMetadataPreservesBothFullWidthValues);
    RUN_TEST(testMetadataRejectsInvalidRuntimeAndClearsOutput);
    RUN_TEST(testMetadataRejectsInvalidSequenceAndClearsOutput);
    RUN_TEST(testSequencerStartsAtOneAndIncrementsForOneRuntime);
    RUN_TEST(testNewStreamStartsAtOneAndBindsCopiedRuntime);
    RUN_TEST(testInvalidRuntimeNeverIssuesOrAdvances);
    RUN_TEST(testMaximumIssuesOnceThenExhaustionIsPermanent);
    RUN_TEST(testSemanticEventSinkDoesNotReceiveMetadata);
}
