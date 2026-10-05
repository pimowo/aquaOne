#include <unity.h>

#include "AquaCore/Web/Realtime.h"

using AquaCore::Identity::RuntimeIdentity;
using AquaCore::Web::RealtimeNotificationMetadata;
using AquaCore::Web::RealtimePublicationPool;
using AquaCore::Web::RealtimePublicationResult;
using AquaCore::Web::RealtimeFrameType;
using AquaCore::Web::RealtimeSequenceIssueResult;
using AquaCore::Web::RealtimeStreamSequence;
using AquaCore::Web::RealtimeStreamSequencer;

namespace AquaCore { namespace Web {
class RealtimeStreamSequencerTestAccess {
public:
    static void setLast(RealtimeStreamSequencer& sequencer, uint64_t value) { sequencer.last_ = value; }
};
} }

namespace {
RuntimeIdentity runtime(uint64_t value) { RuntimeIdentity output; RuntimeIdentity::fromValue(value, output); return output; }

void test_sequence_and_metadata() {
    RealtimeStreamSequence sequence;
    TEST_ASSERT_FALSE(sequence.isValid());
    RealtimeStreamSequencer sequencer(runtime(7U));
    RealtimeNotificationMetadata metadata;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeSequenceIssueResult::Success), static_cast<uint8_t>(sequencer.issue(metadata)));
    TEST_ASSERT_TRUE(metadata.isValid());
    TEST_ASSERT_EQUAL_UINT64(1U, metadata.sequence().value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeSequenceIssueResult::Success), static_cast<uint8_t>(sequencer.issue(metadata)));
    TEST_ASSERT_EQUAL_UINT64(2U, metadata.sequence().value());
}

void test_invalid_metadata_and_exhaustion() {
    RealtimeNotificationMetadata metadata;
    RealtimeStreamSequence sequence;
    TEST_ASSERT_FALSE(RealtimeNotificationMetadata::fromValues(RuntimeIdentity(), sequence, metadata));
    RealtimeStreamSequence::fromValue(1U, sequence);
    TEST_ASSERT_FALSE(RealtimeNotificationMetadata::fromValues(RuntimeIdentity(), sequence, metadata));
    RealtimeStreamSequencer sequencer(runtime(3U));
    AquaCore::Web::RealtimeStreamSequencerTestAccess::setLast(sequencer, UINT64_MAX - 1U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeSequenceIssueResult::Success), static_cast<uint8_t>(sequencer.issue(metadata)));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, metadata.sequence().value());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeSequenceIssueResult::Exhausted), static_cast<uint8_t>(sequencer.issue(metadata)));
}

RealtimeNotificationMetadata validMetadata(uint64_t sequenceValue) {
    RealtimeStreamSequence sequence;
    RealtimeStreamSequence::fromValue(sequenceValue, sequence);
    RealtimeNotificationMetadata metadata;
    (void)RealtimeNotificationMetadata::fromValues(runtime(11U), sequence, metadata);
    return metadata;
}

void test_publication_pool_copies_owned_payload() {
    RealtimePublicationPool<4U, 256U> pool;
    uint8_t source[256U] {};
    source[0] = 1U;
    source[255U] = 4U;
    size_t index = 0U;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RealtimePublicationResult::Accepted),
        static_cast<uint8_t>(pool.acquire(
            validMetadata(1U), RealtimeFrameType::Binary, source,
            sizeof(source), index
        ))
    );
    source[0] = 9U;
    RealtimePublicationPool<4U, 256U>::Entry* entry = pool.entry(index);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_UINT8(1U, entry->payload[0]);
    TEST_ASSERT_EQUAL_UINT8(4U, entry->payload[255U]);
    TEST_ASSERT_EQUAL_UINT32(256U, entry->length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimeFrameType::Binary),
                            static_cast<uint8_t>(entry->type));
}

void test_publication_pool_capacity_and_reuse() {
    RealtimePublicationPool<4U, 256U> pool;
    const uint8_t first[] { 1U, 4U, 5U, 6U };
    const uint8_t second[] { 2U };
    const uint8_t third[] { 3U };
    uint8_t tooLarge[257U] {};
    size_t indices[4U] {};
    size_t reusedIndex = 0U;
    size_t rejectedIndex = 99U;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimePublicationResult::InvalidMetadata),
                            static_cast<uint8_t>(pool.acquire(RealtimeNotificationMetadata(), RealtimeFrameType::Text, first, sizeof(first), rejectedIndex)));
    TEST_ASSERT_EQUAL_UINT32(99U, rejectedIndex);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimePublicationResult::Accepted),
                            static_cast<uint8_t>(pool.acquire(validMetadata(1U), RealtimeFrameType::Text, first, sizeof(first), indices[0])));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimePublicationResult::Accepted),
                            static_cast<uint8_t>(pool.acquire(validMetadata(2U), RealtimeFrameType::Text, second, sizeof(second), indices[1])));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimePublicationResult::Accepted),
                            static_cast<uint8_t>(pool.acquire(validMetadata(3U), RealtimeFrameType::Text, second, sizeof(second), indices[2])));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimePublicationResult::Accepted),
                            static_cast<uint8_t>(pool.acquire(validMetadata(4U), RealtimeFrameType::Text, second, sizeof(second), indices[3])));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimePublicationResult::Busy),
                            static_cast<uint8_t>(pool.acquire(validMetadata(5U), RealtimeFrameType::Text, third, sizeof(third), reusedIndex)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimePublicationResult::PayloadTooLarge),
                            static_cast<uint8_t>(pool.acquire(validMetadata(5U), RealtimeFrameType::Text, tooLarge, sizeof(tooLarge), reusedIndex)));
    pool.release(indices[0]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RealtimePublicationResult::Accepted),
                            static_cast<uint8_t>(pool.acquire(validMetadata(5U), RealtimeFrameType::Text, third, sizeof(third), reusedIndex)));
    TEST_ASSERT_EQUAL_UINT8(indices[0], reusedIndex);
    TEST_ASSERT_EQUAL_UINT32(1U, pool.entry(reusedIndex)->length);
    TEST_ASSERT_EQUAL_UINT8(3U, pool.entry(reusedIndex)->payload[0]);
    TEST_ASSERT_NOT_NULL(pool.entry(indices[1]));
}
}

#if defined(ARDUINO)
void setup() {}
void loop() {}
#else
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sequence_and_metadata);
    RUN_TEST(test_invalid_metadata_and_exhaustion);
    RUN_TEST(test_publication_pool_copies_owned_payload);
    RUN_TEST(test_publication_pool_capacity_and_reuse);
    return UNITY_END();
}
#endif
