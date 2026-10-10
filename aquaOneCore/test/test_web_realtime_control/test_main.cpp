#include <unity.h>
#include <cstring>

#include "../../src/Web/WebSocketControlFrames.h"
#include "AquaCore/Web/RealtimeResync.h"

using namespace AquaCore::Web;
using namespace AquaCore::Web::Internal;

namespace {

struct Sender {
    unsigned calls = 0U;
    uint8_t opcode = 0U;
    uint8_t payload[125U] {};
    size_t length = 0U;
    bool succeeds = true;

    static bool send(void* context, uint8_t type, const uint8_t* data, size_t size) {
        Sender& self = *static_cast<Sender*>(context);
        ++self.calls;
        self.opcode = type;
        self.length = size;
        if (size != 0U) std::memcpy(self.payload, data, size);
        return self.succeeds;
    }
    WebSocketControlResult receive(uint8_t type, const uint8_t* data,
                                   size_t size, bool final = true) {
        return handleWebSocketControl(type, final, data, size, send, this);
    }
};

void assertResult(WebSocketControlResult expected, WebSocketControlResult actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void test_ping_echoes_payload_as_pong() {
    Sender sender;
    const uint8_t payload[] {'h', 'i', 'l', '2'};
    assertResult(WebSocketControlResult::Handled,
                 sender.receive(0x9U, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0xAU, sender.opcode);
    TEST_ASSERT_EQUAL_UINT32(1U, sender.calls);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), sender.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, sender.payload, sizeof(payload));
}

void test_empty_ping_echoes_empty_pong() {
    Sender sender;
    assertResult(WebSocketControlResult::Handled, sender.receive(0x9U, nullptr, 0U));
    TEST_ASSERT_EQUAL_UINT8(0xAU, sender.opcode);
    TEST_ASSERT_EQUAL_UINT32(0U, sender.length);
}

void test_maximum_ping_is_bounded_and_binary_safe() {
    Sender sender;
    uint8_t payload[125U];
    for (size_t i = 0U; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i);
    assertResult(WebSocketControlResult::Handled,
                 sender.receive(0x9U, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0xAU, sender.opcode);
    TEST_ASSERT_EQUAL_UINT32(125U, sender.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, sender.payload, sizeof(payload));
}

void test_incoming_pong_has_no_reply() {
    Sender sender;
    const uint8_t payload[] {0xFFU, 0U};
    assertResult(WebSocketControlResult::Handled, sender.receive(0xAU, payload, 2U));
    assertResult(WebSocketControlResult::Handled, sender.receive(0xAU, nullptr, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, sender.calls);
}

void test_close_echoes_code_and_utf8_reason() {
    Sender sender;
    const uint8_t payload[] {0x03U, 0xE8U, 'o', 'k', 0xC4U, 0x85U};
    assertResult(WebSocketControlResult::Handled, sender.receive(0x8U, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0x8U, sender.opcode);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), sender.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, sender.payload, sizeof(payload));
    assertResult(WebSocketControlResult::Handled, sender.receive(0x8U, nullptr, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, sender.length);
}

void test_invalid_control_headers_are_rejected_without_send() {
    Sender sender;
    uint8_t payload[126U] {};
    for (uint8_t opcode = 0x8U; opcode <= 0xAU; ++opcode) {
        assertResult(WebSocketControlResult::Malformed, sender.receive(opcode, payload, 126U));
        assertResult(WebSocketControlResult::Malformed, sender.receive(opcode, nullptr, 1U));
        assertResult(WebSocketControlResult::Malformed, sender.receive(opcode, payload, 0U, false));
    }
    TEST_ASSERT_EQUAL_UINT32(0U, sender.calls);
}

void test_invalid_close_code_and_reason_are_rejected() {
    Sender sender;
    const uint8_t oneByte[] {0x03U};
    assertResult(WebSocketControlResult::Malformed, sender.receive(0x8U, oneByte, 1U));
    const uint16_t invalidCodes[] {999U, 1004U, 1005U, 1006U, 1015U, 2000U, 5000U};
    for (uint16_t code : invalidCodes) {
        const uint8_t payload[] {static_cast<uint8_t>(code >> 8U), static_cast<uint8_t>(code)};
        assertResult(WebSocketControlResult::Malformed, sender.receive(0x8U, payload, 2U));
    }
    const uint8_t invalidUtf8[][6] {
        {3U, 232U, 0xC0U, 0x80U, 0U, 0U}, // overlong
        {3U, 232U, 0xEDU, 0xA0U, 0x80U, 0U}, // surrogate
        {3U, 232U, 0xF4U, 0x90U, 0x80U, 0x80U}, // beyond Unicode
        {3U, 232U, 0xE2U, 0U, 0U, 0U} // incomplete
    };
    const size_t lengths[] {4U, 5U, 6U, 3U};
    for (size_t i = 0U; i < 4U; ++i)
        assertResult(WebSocketControlResult::Malformed, sender.receive(0x8U, invalidUtf8[i], lengths[i]));
    TEST_ASSERT_EQUAL_UINT32(0U, sender.calls);
}

void test_application_opcodes_remain_unsupported() {
    Sender sender;
    const uint8_t payload[] {'{', '}'};
    const uint8_t opcodes[] {0x0U, 0x1U, 0x2U, 0xBU};
    for (uint8_t opcode : opcodes)
        assertResult(WebSocketControlResult::Unsupported, sender.receive(opcode, payload, 2U));
    TEST_ASSERT_EQUAL_UINT32(0U, sender.calls);
}

void test_send_failure_is_terminal_for_this_callback() {
    Sender sender;
    sender.succeeds = false;
    assertResult(WebSocketControlResult::SendFailed, sender.receive(0x9U, nullptr, 0U));
    assertResult(WebSocketControlResult::SendFailed, sender.receive(0x8U, nullptr, 0U));
    assertResult(WebSocketControlResult::Handled, sender.receive(0xAU, nullptr, 0U));
    TEST_ASSERT_EQUAL_UINT32(2U, sender.calls);
}

void test_control_frames_do_not_issue_sequence_or_request_recovery() {
    AquaCore::Identity::RuntimeIdentity runtime;
    TEST_ASSERT_TRUE(AquaCore::Identity::RuntimeIdentity::fromValue(7U, runtime));
    RealtimeStreamSequencer sequencer(runtime);
    RealtimeRecoveryState recovery;
    Sender sender;
    assertResult(WebSocketControlResult::Handled, sender.receive(0x9U, nullptr, 0U));
    assertResult(WebSocketControlResult::Handled, sender.receive(0xAU, nullptr, 0U));
    TEST_ASSERT_TRUE(sequencer.currentPosition().isBeforeFirst());
    TEST_ASSERT_FALSE(recovery.isRequired());
    RealtimeNotificationMetadata notification;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RealtimeSequenceIssueResult::Success),
                          static_cast<int>(sequencer.issue(notification)));
    TEST_ASSERT_TRUE(notification.sequence().value() == 1U);
}

void test_inactive_close_cleanup_preserves_healthy_client_and_generations() {
    RealtimeClientRegistry<2U> clients;
    RealtimeClientToken closing, healthy;
    TEST_ASSERT_TRUE(clients.connect(10, RealtimeStreamPosition::beforeFirst(), closing));
    TEST_ASSERT_TRUE(clients.connect(11, RealtimeStreamPosition::beforeFirst(), healthy));
    TEST_ASSERT_TRUE(clients.markerSucceeded(closing, false));
    TEST_ASSERT_TRUE(clients.markerSucceeded(healthy, false));
    Sender sender;
    assertResult(WebSocketControlResult::Handled, sender.receive(0x8U, nullptr, 0U));
    TEST_ASSERT_TRUE(clients.reclaimInactive(closing, 10));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RealtimeClientState::Live),
                          static_cast<int>(clients.slot(healthy.slot)->state));
    RealtimeClientToken reused;
    TEST_ASSERT_TRUE(clients.connect(10, RealtimeStreamPosition::beforeFirst(), reused));
    TEST_ASSERT_FALSE(clients.isTokenCurrent(closing));
    TEST_ASSERT_FALSE(clients.reclaimInactive(closing, 10));
    TEST_ASSERT_TRUE(clients.isTokenCurrentForFd(reused, 10));
}

} // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ping_echoes_payload_as_pong);
    RUN_TEST(test_empty_ping_echoes_empty_pong);
    RUN_TEST(test_maximum_ping_is_bounded_and_binary_safe);
    RUN_TEST(test_incoming_pong_has_no_reply);
    RUN_TEST(test_close_echoes_code_and_utf8_reason);
    RUN_TEST(test_invalid_control_headers_are_rejected_without_send);
    RUN_TEST(test_invalid_close_code_and_reason_are_rejected);
    RUN_TEST(test_application_opcodes_remain_unsupported);
    RUN_TEST(test_send_failure_is_terminal_for_this_callback);
    RUN_TEST(test_control_frames_do_not_issue_sequence_or_request_recovery);
    RUN_TEST(test_inactive_close_cleanup_preserves_healthy_client_and_generations);
    return UNITY_END();
}
