#include <unity.h>

#include <stdint.h>

#include "AquaCore/Web/PublishedSnapshot.h"

using AquaCore::Web::PublishedSnapshot;
using AquaCore::Web::SnapshotSynchronizer;

namespace {

struct Snapshot {
    uint32_t sequence;
    uint32_t inverse;
    uint32_t checksum;
    uint32_t payload[16];
};

Snapshot makeSnapshot(uint32_t sequence) {
    Snapshot value {};
    value.sequence = sequence;
    value.inverse = ~sequence;
    value.checksum = sequence ^ 0xA53C7E19U;
    for (uint32_t i = 0U; i < 16U; ++i) {
        value.payload[i] = sequence + i;
    }
    return value;
}

bool coherent(const Snapshot& value) {
    if (value.inverse != ~value.sequence ||
        value.checksum != (value.sequence ^ 0xA53C7E19U)) {
        return false;
    }
    for (uint32_t i = 0U; i < 16U; ++i) {
        if (value.payload[i] != value.sequence + i) {
            return false;
        }
    }
    return true;
}

} // namespace

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include "AquaCore/Web/Esp32SnapshotSynchronizer.h"
#include "AquaCore/Web/EspIdfWebTransport.h"

// Compile/link proof only: no network or HIL is started.
volatile bool startTransportProof = false;
void setup() {
    UNITY_BEGIN();
    AquaCore::Web::Esp32SnapshotSynchronizer synchronizer;
    PublishedSnapshot<Snapshot> publication(synchronizer);
    Snapshot local {};
    if (synchronizer.isReady()) {
        publication.publish(makeSnapshot(1U));
        publication.read(local);
        publication.invalidate();
    }
    AquaCore::Web::EspIdfWebTransport transport;
    if (startTransportProof) {
        transport.begin(80U);
    }
    transport.stop();
    UNITY_END();
}
void loop() {}

#else

#include <atomic>
#include <mutex>
#include <thread>

namespace {

class NativeSynchronizer final : public SnapshotSynchronizer {
public:
    bool lock() override {
        if (failNext_.exchange(false)) {
            return false;
        }
        mutex_.lock();
        held_.store(true);
        acquired_.fetch_add(1U);
        return true;
    }

    void unlock() override {
        held_.store(false);
        released_.fetch_add(1U);
        mutex_.unlock();
    }

    void failNext() { failNext_.store(true); }
    bool isHeld() const { return held_.load(); }
    unsigned acquired() const { return acquired_.load(); }
    unsigned released() const { return released_.load(); }

private:
    std::mutex mutex_;
    std::atomic<bool> failNext_ {false};
    std::atomic<bool> held_ {false};
    std::atomic<unsigned> acquired_ {0U};
    std::atomic<unsigned> released_ {0U};
};

void test_initial_unavailable_and_invalidation() {
    NativeSynchronizer synchronizer;
    PublishedSnapshot<Snapshot> publication(synchronizer);
    Snapshot output = makeSnapshot(77U);
    TEST_ASSERT_FALSE(publication.read(output));
    TEST_ASSERT_EQUAL_UINT32(77U, output.sequence);
    TEST_ASSERT_TRUE(publication.publish(makeSnapshot(1U)));
    TEST_ASSERT_TRUE(publication.read(output));
    TEST_ASSERT_EQUAL_UINT32(1U, output.sequence);
    TEST_ASSERT_TRUE(publication.invalidate());
    TEST_ASSERT_FALSE(publication.read(output));
    TEST_ASSERT_EQUAL_UINT32(1U, output.sequence);
    TEST_ASSERT_TRUE(publication.publish(makeSnapshot(2U)));
    TEST_ASSERT_TRUE(publication.read(output));
    TEST_ASSERT_EQUAL_UINT32(2U, output.sequence);
    TEST_ASSERT_EQUAL_UINT32(synchronizer.acquired(), synchronizer.released());
}

void test_caller_copy_is_independent() {
    NativeSynchronizer synchronizer;
    PublishedSnapshot<Snapshot> publication(synchronizer);
    TEST_ASSERT_TRUE(publication.publish(makeSnapshot(10U)));
    Snapshot first {};
    TEST_ASSERT_TRUE(publication.read(first));
    TEST_ASSERT_TRUE(publication.publish(makeSnapshot(11U)));
    TEST_ASSERT_EQUAL_UINT32(10U, first.sequence);
    first.sequence = 99U;
    Snapshot second {};
    TEST_ASSERT_TRUE(publication.read(second));
    TEST_ASSERT_EQUAL_UINT32(11U, second.sequence);
    TEST_ASSERT_TRUE(coherent(second));
}

void test_lock_failure_preserves_publication() {
    NativeSynchronizer synchronizer;
    PublishedSnapshot<Snapshot> publication(synchronizer);
    synchronizer.failNext();
    TEST_ASSERT_FALSE(publication.publish(makeSnapshot(1U)));
    Snapshot output = makeSnapshot(99U);
    TEST_ASSERT_FALSE(publication.read(output));
    TEST_ASSERT_EQUAL_UINT32(99U, output.sequence);
    TEST_ASSERT_TRUE(publication.publish(makeSnapshot(2U)));
    synchronizer.failNext();
    TEST_ASSERT_FALSE(publication.invalidate());
    synchronizer.failNext();
    TEST_ASSERT_FALSE(publication.read(output));
    TEST_ASSERT_EQUAL_UINT32(99U, output.sequence);
    TEST_ASSERT_TRUE(publication.read(output));
    TEST_ASSERT_EQUAL_UINT32(2U, output.sequence);
    TEST_ASSERT_EQUAL_UINT32(synchronizer.acquired(), synchronizer.released());
}

void test_independent_typed_resources() {
    struct OtherSnapshot { uint32_t value; };
    NativeSynchronizer synchronizer;
    PublishedSnapshot<Snapshot> system(synchronizer);
    PublishedSnapshot<OtherSnapshot> diagnostics(synchronizer);
    OtherSnapshot other {42U};
    TEST_ASSERT_TRUE(system.publish(makeSnapshot(3U)));
    TEST_ASSERT_FALSE(diagnostics.read(other));
    TEST_ASSERT_EQUAL_UINT32(42U, other.value);
    TEST_ASSERT_TRUE(diagnostics.publish(OtherSnapshot {7U}));
    Snapshot systemCopy {};
    TEST_ASSERT_TRUE(system.read(systemCopy));
    TEST_ASSERT_TRUE(diagnostics.read(other));
    TEST_ASSERT_EQUAL_UINT32(3U, systemCopy.sequence);
    TEST_ASSERT_EQUAL_UINT32(7U, other.value);
}

// A route fixture reads once, then serializes only its caller-owned copy.
uint32_t serializeCopy(const Snapshot& local, const NativeSynchronizer& sync) {
    return sync.isHeld() ? 0U : local.sequence;
}

void test_serialization_after_unlock() {
    NativeSynchronizer synchronizer;
    PublishedSnapshot<Snapshot> publication(synchronizer);
    TEST_ASSERT_TRUE(publication.publish(makeSnapshot(5U)));
    Snapshot local {};
    TEST_ASSERT_TRUE(publication.read(local));
    TEST_ASSERT_FALSE(synchronizer.isHeld());
    TEST_ASSERT_TRUE(publication.publish(makeSnapshot(6U)));
    TEST_ASSERT_EQUAL_UINT32(5U, serializeCopy(local, synchronizer));
}

void test_concurrent_reader_never_observes_torn_copy() {
    NativeSynchronizer synchronizer;
    PublishedSnapshot<Snapshot> publication(synchronizer);
    std::atomic<bool> start {false};
    std::atomic<bool> done {false};
    std::atomic<bool> valid {true};
    std::atomic<unsigned> observations {0U};
    std::thread writer([&]() {
        while (!start.load()) { std::this_thread::yield(); }
        for (uint32_t i = 1U; i <= 3000U; ++i) {
            if (!publication.publish(makeSnapshot(i))) {
                valid.store(false);
                break;
            }
            if ((i & 15U) == 0U) { std::this_thread::yield(); }
        }
        done.store(true);
    });
    std::thread reader([&]() {
        while (!start.load()) { std::this_thread::yield(); }
        while (!done.load() || observations.load() < 100U) {
            Snapshot local {};
            if (publication.read(local)) {
                observations.fetch_add(1U);
                if (!coherent(local)) { valid.store(false); }
            }
            std::this_thread::yield();
        }
    });
    start.store(true);
    writer.join();
    reader.join();
    TEST_ASSERT_TRUE(valid.load());
    TEST_ASSERT_TRUE(observations.load() >= 100U);
    TEST_ASSERT_EQUAL_UINT32(synchronizer.acquired(), synchronizer.released());
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_unavailable_and_invalidation);
    RUN_TEST(test_caller_copy_is_independent);
    RUN_TEST(test_lock_failure_preserves_publication);
    RUN_TEST(test_independent_typed_resources);
    RUN_TEST(test_serialization_after_unlock);
    RUN_TEST(test_concurrent_reader_never_observes_torn_copy);
    return UNITY_END();
}

#endif
