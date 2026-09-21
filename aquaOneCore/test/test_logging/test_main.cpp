#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <unity.h>
#if defined(ARDUINO)
#include "AquaCore/Logging/SerialLogSink.h"
#endif

#include <stdint.h>
#include <string.h>

#include "AquaCore/Logging/Logger.h"

using namespace AquaCore;

void setUp() {
}

void tearDown() {
}

namespace {

struct CapturedLog {
    LogLevel level;
    char module[32];
    char message[96];
};

class CaptureSink final : public LogSink {
public:
    void write(
        LogLevel level,
        const char* module,
        const char* message
    ) override {
        if (count >= MAX_ENTRIES) {
            return;
        }

        entries[count].level = level;
        copy(entries[count].module, sizeof(entries[count].module), module);
        copy(entries[count].message, sizeof(entries[count].message), message);
        ++count;
    }

    static constexpr uint8_t MAX_ENTRIES = 16U;
    CapturedLog entries[MAX_ENTRIES] {};
    uint8_t count = 0U;

private:
    static void copy(
        char* destination,
        size_t capacity,
        const char* source
    ) {
        if (capacity == 0U) {
            return;
        }

        if (source == nullptr) {
            destination[0] = '\0';
            return;
        }

        strncpy(destination, source, capacity - 1U);
        destination[capacity - 1U] = '\0';
    }
};

#if defined(ARDUINO)

class CapturePrint final : public Print {
public:
    size_t write(uint8_t value) override {
        if (length + 1U < sizeof(buffer)) {
            buffer[length++] = static_cast<char>(value);
            buffer[length] = '\0';
        }

        return 1U;
    }

    char buffer[160] {};
    size_t length = 0U;
};

#endif

#if AQUA_CORE_LOGGING_ENABLED

void test_default_logger_works() {
    Logger logger;
    CaptureSink sink;

    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LogLevel::Info),
        static_cast<int>(logger.level())
    );

    logger.info("System", "without sink");
    TEST_ASSERT_EQUAL_UINT8(0U, sink.count);

    logger.setSink(sink);
    logger.info("System", "ready");
    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
}

void test_debug_level_passes_all_message_levels() {
    CaptureSink sink;
    Logger logger(sink);
    logger.setLevel(LogLevel::Debug);

    logger.debug("M", "debug");
    logger.info("M", "info");
    logger.warning("M", "warning");
    logger.error("M", "error");

    TEST_ASSERT_EQUAL_UINT8(4U, sink.count);
}

void test_info_level_blocks_debug() {
    CaptureSink sink;
    Logger logger(sink);
    logger.setLevel(LogLevel::Info);

    logger.debug("M", "debug");
    logger.info("M", "info");

    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LogLevel::Info),
        static_cast<int>(sink.entries[0].level)
    );
}

void test_warning_level_blocks_debug_and_info() {
    CaptureSink sink;
    Logger logger(sink);
    logger.setLevel(LogLevel::Warning);

    logger.debug("M", "debug");
    logger.info("M", "info");
    logger.warning("M", "warning");
    logger.error("M", "error");

    TEST_ASSERT_EQUAL_UINT8(2U, sink.count);
}

void test_error_level_passes_only_error() {
    CaptureSink sink;
    Logger logger(sink);
    logger.setLevel(LogLevel::Error);

    logger.debug("M", "debug");
    logger.info("M", "info");
    logger.warning("M", "warning");
    logger.error("M", "error");

    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LogLevel::Error),
        static_cast<int>(sink.entries[0].level)
    );
}

void test_off_level_blocks_everything() {
    CaptureSink sink;
    Logger logger(sink);
    logger.setLevel(LogLevel::Off);

    logger.debug("M", "debug");
    logger.info("M", "info");
    logger.warning("M", "warning");
    logger.error("M", "error");
    logger.log(LogLevel::Off, "M", "off");

    TEST_ASSERT_EQUAL_UINT8(0U, sink.count);
}

void test_module_is_forwarded() {
    CaptureSink sink;
    Logger logger(sink);

    logger.info("Storage", "ready");

    TEST_ASSERT_EQUAL_STRING("Storage", sink.entries[0].module);
}

void test_message_is_forwarded() {
    CaptureSink sink;
    Logger logger(sink);

    logger.info("System", "boot complete");

    TEST_ASSERT_EQUAL_STRING(
        "boot complete",
        sink.entries[0].message
    );
}

void test_two_loggers_can_have_different_levels() {
    CaptureSink firstSink;
    CaptureSink secondSink;
    Logger first(firstSink);
    Logger second(secondSink);

    first.setLevel(LogLevel::Debug);
    second.setLevel(LogLevel::Warning);
    first.info("M", "first");
    second.info("M", "second");

    TEST_ASSERT_EQUAL_UINT8(1U, firstSink.count);
    TEST_ASSERT_EQUAL_UINT8(0U, secondSink.count);
}

void test_threshold_change_is_deterministic() {
    CaptureSink sink;
    Logger logger(sink);

    logger.setLevel(LogLevel::Warning);
    logger.info("M", "blocked");
    logger.warning("M", "accepted");
    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LogLevel::Warning),
        static_cast<int>(sink.entries[0].level)
    );

    logger.setLevel(LogLevel::Debug);
    logger.debug("M", "now accepted");
    TEST_ASSERT_EQUAL_UINT8(2U, sink.count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LogLevel::Debug),
        static_cast<int>(sink.entries[1].level)
    );
}

void test_two_loggers_can_have_different_sinks() {
    CaptureSink firstSink;
    CaptureSink secondSink;
    Logger first(firstSink);
    Logger second(secondSink);

    first.info("A", "first");
    second.info("B", "second");

    TEST_ASSERT_EQUAL_UINT8(1U, firstSink.count);
    TEST_ASSERT_EQUAL_UINT8(1U, secondSink.count);
    TEST_ASSERT_EQUAL_STRING("A", firstSink.entries[0].module);
    TEST_ASSERT_EQUAL_STRING("B", secondSink.entries[0].module);
}

void test_sink_can_be_changed_at_runtime() {
    CaptureSink firstSink;
    CaptureSink secondSink;
    Logger logger(firstSink);

    logger.info("M", "first");
    logger.setSink(secondSink);
    logger.info("M", "second");

    TEST_ASSERT_EQUAL_UINT8(1U, firstSink.count);
    TEST_ASSERT_EQUAL_UINT8(1U, secondSink.count);
    TEST_ASSERT_EQUAL_STRING(
        "second",
        secondSink.entries[0].message
    );
}

void test_debug_helper() {
    CaptureSink sink;
    Logger logger(sink);
    logger.setLevel(LogLevel::Debug);

    logger.debug("M", "debug");

    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LogLevel::Debug),
        static_cast<int>(sink.entries[0].level)
    );
}

void test_info_helper() {
    CaptureSink sink;
    Logger logger(sink);

    logger.info("M", "info");

    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LogLevel::Info),
        static_cast<int>(sink.entries[0].level)
    );
}

void test_warning_helper() {
    CaptureSink sink;
    Logger logger(sink);

    logger.warning("M", "warning");

    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LogLevel::Warning),
        static_cast<int>(sink.entries[0].level)
    );
}

void test_error_helper() {
    CaptureSink sink;
    Logger logger(sink);

    logger.error("M", "error");

    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(LogLevel::Error),
        static_cast<int>(sink.entries[0].level)
    );
}

void test_null_module_is_safe() {
    CaptureSink sink;
    Logger logger(sink);

    logger.info(nullptr, "message");

    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
    TEST_ASSERT_EQUAL_STRING("", sink.entries[0].module);
}

void test_null_message_is_safe() {
    CaptureSink sink;
    Logger logger(sink);

    logger.info("System", nullptr);

    TEST_ASSERT_EQUAL_UINT8(1U, sink.count);
    TEST_ASSERT_EQUAL_STRING("", sink.entries[0].message);
}

#if defined(ARDUINO)

void test_serial_sink_uses_common_format() {
    CapturePrint output;
    SerialLogSink sink(output);

    sink.write(LogLevel::Warning, "Time", "RTC invalid");

    TEST_ASSERT_EQUAL_STRING(
        "[WARN][Time] RTC invalid\r\n",
        output.buffer
    );
}

#endif

void test_logger_does_not_modify_inputs() {
    CaptureSink sink;
    Logger logger(sink);
    char module[] = "System";
    char message[] = "boot complete";
    char moduleBefore[sizeof(module)] {};
    char messageBefore[sizeof(message)] {};
    memcpy(moduleBefore, module, sizeof(module));
    memcpy(messageBefore, message, sizeof(message));

    logger.info(module, message);

    TEST_ASSERT_EQUAL_MEMORY(
        moduleBefore,
        module,
        sizeof(module)
    );
    TEST_ASSERT_EQUAL_MEMORY(
        messageBefore,
        message,
        sizeof(message)
    );
}

void test_compile_time_configuration_is_available() {
    TEST_ASSERT_TRUE(Logger::compiledIn());
    TEST_ASSERT_EQUAL_INT(1, AQUA_CORE_LOGGING_ENABLED);
}

#else

void test_compile_time_disabled_api_remains_available() {
    CaptureSink sink;
    Logger logger(sink);

    logger.setLevel(LogLevel::Debug);
    logger.debug("M", "debug");
    logger.info(nullptr, nullptr);
    logger.warning("M", "warning");
    logger.error("M", "error");
    logger.log(LogLevel::Error, "M", "direct");

    TEST_ASSERT_FALSE(Logger::compiledIn());
    TEST_ASSERT_EQUAL_UINT8(0U, sink.count);
}

#endif

} // namespace

void runTests() {
    UNITY_BEGIN();

#if AQUA_CORE_LOGGING_ENABLED
    RUN_TEST(test_default_logger_works);
    RUN_TEST(test_debug_level_passes_all_message_levels);
    RUN_TEST(test_info_level_blocks_debug);
    RUN_TEST(test_warning_level_blocks_debug_and_info);
    RUN_TEST(test_error_level_passes_only_error);
    RUN_TEST(test_off_level_blocks_everything);
    RUN_TEST(test_module_is_forwarded);
    RUN_TEST(test_message_is_forwarded);
    RUN_TEST(test_two_loggers_can_have_different_levels);
    RUN_TEST(test_two_loggers_can_have_different_sinks);
    RUN_TEST(test_sink_can_be_changed_at_runtime);
    RUN_TEST(test_debug_helper);
    RUN_TEST(test_info_helper);
    RUN_TEST(test_warning_helper);
    RUN_TEST(test_error_helper);
    RUN_TEST(test_null_module_is_safe);
    RUN_TEST(test_null_message_is_safe);
#if defined(ARDUINO)
    RUN_TEST(test_serial_sink_uses_common_format);
#endif
    RUN_TEST(test_logger_does_not_modify_inputs);
    RUN_TEST(test_compile_time_configuration_is_available);
#else
    RUN_TEST(test_compile_time_disabled_api_remains_available);
#endif

}

#if defined(ARDUINO)

void setup() {
    delay(2000);
    runTests();
    UNITY_END();
}

void loop() {
}

#else

int main() {
    runTests();
    return UNITY_END();
}

#endif