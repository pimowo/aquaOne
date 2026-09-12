#include <Arduino.h>
#include <unity.h>

#include <cstring>

#include "AquaCore/Web/WebBackend.h"
#include "AquaCore/Web/WebConfig.h"
#include "AquaCore/Web/WebService.h"

#include "../../src/DoserWebRuntime.h"
#include "../../src/WebManager.cpp"

using namespace AquaCore::Web;

namespace {

class Writer final : public WebResponseWriter {
public:
    bool beginResponse(uint16_t value, ContentType type) override {
        if (begun) return false;
        begun = true;
        status = value;
        contentType = type;
        return true;
    }

    bool write(const char* data, size_t length) override {
        if (!begun || ended || data == nullptr || size + length >= sizeof(body))
            return false;
        memcpy(body + size, data, length);
        size += length;
        body[size] = '\0';
        return true;
    }

    bool endResponse() override {
        if (!begun || ended) return false;
        ended = true;
        return true;
    }

    void clear() {
        begun = false;
        ended = false;
        status = 0U;
        contentType = ContentType::PlainText;
        size = 0U;
        body[0] = '\0';
    }

    bool begun = false;
    bool ended = false;
    uint16_t status = 0U;
    ContentType contentType = ContentType::PlainText;
    char body[4096] {};
    size_t size = 0U;
};

struct Header {
    const char* name;
    const char* value;
};

class RequestContext final : public WebRequestContext {
public:
    RequestContext(const Header* headersValue, size_t headerCountValue,
                   const char* usernameValue, const char* passwordValue,
                   Writer& writerValue)
        : headers(headersValue),
          headerCount(headerCountValue),
          username(usernameValue),
          password(passwordValue),
          writer(writerValue) {
    }

    bool hasHeader(const char* name) const override {
        if (name == nullptr) return false;
        for (size_t index = 0U; index < headerCount; ++index)
            if (strcmp(headers[index].name, name) == 0) return true;
        return false;
    }

    size_t copyHeader(const char* name, char* output,
                      size_t outputSize) const override {
        if (output != nullptr && outputSize > 0U) output[0] = '\0';
        if (name == nullptr) return 0U;
        for (size_t index = 0U; index < headerCount; ++index) {
            if (strcmp(headers[index].name, name) != 0) continue;
            const size_t length = strlen(headers[index].value);
            if (output != nullptr && outputSize > 0U) {
                const size_t copied = length < outputSize - 1U
                    ? length : outputSize - 1U;
                memcpy(output, headers[index].value, copied);
                output[copied] = '\0';
            }
            return length;
        }
        return 0U;
    }

    bool authenticateBasic(const char* expectedUser,
                           const char* expectedPassword) const override {
        return username != nullptr && password != nullptr &&
            expectedUser != nullptr && expectedPassword != nullptr &&
            strcmp(username, expectedUser) == 0 &&
            strcmp(password, expectedPassword) == 0;
    }

    bool requestBasicAuthentication(const char* realm) const override {
        challenged = true;
        strncpy(challengeRealm, realm != nullptr ? realm : "",
                sizeof(challengeRealm) - 1U);
        writer.beginResponse(401U, ContentType::PlainText);
        writer.writeText("Unauthorized");
        writer.endResponse();
        return true;
    }

    const Header* headers;
    size_t headerCount;
    const char* username;
    const char* password;
    Writer& writer;
    mutable bool challenged = false;
    mutable char challengeRealm[32] {};
};

class HttpBackend final : public WebBackend {
public:
    struct Route {
        char path[64] {};
        HttpMethod method = HttpMethod::Get;
        WebRouteHandler handler = nullptr;
        void* context = nullptr;
        WebRouteOptions options {};
        size_t uploaded = 0U;
    };

    bool addRoute(const char* path, HttpMethod method,
                  WebRouteHandler handler, void* context) override {
        return addRoute(path, method, handler, context, WebRouteOptions {});
    }

    bool addRoute(const char* path, HttpMethod method,
                  WebRouteHandler handler, void* context,
                  const WebRouteOptions& options) override {
        if (path == nullptr || handler == nullptr || count >= 12U) return false;
        for (size_t index = 0U; index < count; ++index)
            if (routes[index].method == method &&
                strcmp(routes[index].path, path) == 0) return false;
        strncpy(routes[count].path, path, sizeof(routes[count].path) - 1U);
        routes[count].method = method;
        routes[count].handler = handler;
        routes[count].context = context;
        routes[count].options = options;
        ++count;
        return true;
    }

    bool setNotFoundHandler(WebRouteHandler handler, void* context) override {
        notFound = handler;
        notFoundContext = context;
        return handler != nullptr;
    }

    bool begin(uint16_t value) override {
        port = value;
        running = true;
        return true;
    }

    void update() override { ++updates; }
    void stop() override { running = false; }
    bool isRunning() const override { return running; }

    bool hasRoute(const char* path, HttpMethod method) const {
        for (size_t index = 0U; index < count; ++index)
            if (routes[index].method == method &&
                strcmp(routes[index].path, path) == 0) return true;
        return false;
    }

    void request(const char* path, HttpMethod method,
                 const char* username = nullptr,
                 const char* password = nullptr) {
        output.clear();
        RequestContext requestContext(
            nullptr, 0U, username, password, output
        );
        WebRequest request {method, path, nullptr, 0U, &requestContext};
        Route* route = find(path, method);
        if (route != nullptr) route->handler(route->context, request, output);
        else if (notFound != nullptr)
            notFound(notFoundContext, request, output);
        challenged = requestContext.challenged;
        strncpy(challengeRealm, requestContext.challengeRealm,
                sizeof(challengeRealm) - 1U);
    }

    void upload(const char* path, WebUploadStatus status,
                const char* filename, const uint8_t* data,
                size_t dataLength, const Header* headers,
                size_t headerCount, const char* username,
                const char* password) {
        Route* route = find(path, HttpMethod::Post);
        TEST_ASSERT_NOT_NULL(route);
        TEST_ASSERT_NOT_NULL(route->options.uploadHandler);
        if (status == WebUploadStatus::Start) route->uploaded = 0U;
        if (status == WebUploadStatus::Chunk) route->uploaded += dataLength;
        RequestContext requestContext(
            headers, headerCount, username, password, output
        );
        WebRequest request {
            HttpMethod::Post, path, nullptr, 0U, &requestContext
        };
        const WebUploadEvent event {
            status, filename, data, dataLength, route->uploaded
        };
        route->options.uploadHandler(
            route->options.uploadContext, request, event
        );
        if (status == WebUploadStatus::End ||
            status == WebUploadStatus::Abort) route->uploaded = 0U;
    }

    Route routes[12] {};
    size_t count = 0U;
    Writer output;
    WebRouteHandler notFound = nullptr;
    void* notFoundContext = nullptr;
    bool running = false;
    uint16_t port = 0U;
    uint16_t updates = 0U;
    bool challenged = false;
    char challengeRealm[32] {};

private:
    Route* find(const char* path, HttpMethod method) {
        for (size_t index = 0U; index < count; ++index)
            if (routes[index].method == method &&
                strcmp(routes[index].path, path) == 0) return &routes[index];
        return nullptr;
    }
};

class FakeRuntime final : public WebManagerRuntime {
public:
    unsigned long nowMs() const override { return now; }
    size_t availableFirmwareSpace() const override { return available; }
    bool beginFirmwareUpdate(size_t expectedSize) override {
        ++beginCalls;
        begunSize = expectedSize;
        return beginResult;
    }
    size_t writeFirmware(const uint8_t*, size_t length) override {
        ++writeCalls;
        written += length;
        return writeResult ? length : 0U;
    }
    bool endFirmwareUpdate() override {
        ++endCalls;
        return endResult;
    }
    void abortFirmwareUpdate() override { ++abortCalls; }
    const char* firmwareError() const override { return "fake update error"; }
    void stopPumps() override { ++stopCalls; }
    void setOtaInProgress(bool value) override {
        maintenance = value;
        ++maintenanceCalls;
    }
    void serviceDuringUpload() override { ++serviceCalls; }
    void restartDevice() override { ++restartCalls; }

    unsigned long now = 100U;
    size_t available = 4096U;
    bool beginResult = true;
    bool writeResult = true;
    bool endResult = true;
    bool maintenance = false;
    size_t begunSize = 0U;
    size_t written = 0U;
    uint16_t beginCalls = 0U;
    uint16_t writeCalls = 0U;
    uint16_t endCalls = 0U;
    uint16_t abortCalls = 0U;
    uint16_t stopCalls = 0U;
    uint16_t maintenanceCalls = 0U;
    uint16_t serviceCalls = 0U;
    uint16_t restartCalls = 0U;
};

struct Fixture {
    Fixture(const char* password = "secret")
        : web(backend) {
        TEST_ASSERT_TRUE(manager.begin(web, runtime, "admin", password));
        WebConfig config {};
        config.enabled = true;
        config.port = 80U;
        config.navigationMask = 0U;
        TEST_ASSERT_TRUE(web.begin(config));
    }

    void upload(WebUploadStatus status, const char* filename = "firmware.bin",
                const uint8_t* data = nullptr, size_t length = 0U,
                const char* size = "3", const char* password = "secret") {
        const Header header {"X-Firmware-Size", size};
        backend.upload(
            "/update", status, filename, data, length,
            size != nullptr ? &header : nullptr, size != nullptr ? 1U : 0U,
            "admin", password
        );
    }

    HttpBackend backend;
    WebService web;
    FakeRuntime runtime;
    WebManager manager;
};

void test_only_legacy_admin_routes_are_registered_by_doser() {
    Fixture fixture;
    TEST_ASSERT_TRUE(fixture.backend.hasRoute("/api/restart", HttpMethod::Post));
    TEST_ASSERT_TRUE(fixture.backend.hasRoute("/update", HttpMethod::Get));
    TEST_ASSERT_TRUE(fixture.backend.hasRoute("/update", HttpMethod::Post));
    TEST_ASSERT_FALSE(fixture.backend.hasRoute("/", HttpMethod::Get));
    TEST_ASSERT_FALSE(fixture.backend.hasRoute("/api/status", HttpMethod::Get));
}

void test_restart_auth_and_delayed_execution() {
    Fixture fixture;
    fixture.backend.request("/api/restart", HttpMethod::Post);
    TEST_ASSERT_EQUAL_UINT16(401U, fixture.backend.output.status);
    TEST_ASSERT_TRUE(fixture.backend.challenged);
    TEST_ASSERT_EQUAL_STRING("PMW AquaDoser", fixture.backend.challengeRealm);
    TEST_ASSERT_FALSE(fixture.manager.isRestartPending());

    fixture.backend.request("/api/restart", HttpMethod::Post, "admin", "wrong");
    TEST_ASSERT_EQUAL_UINT16(401U, fixture.backend.output.status);
    TEST_ASSERT_FALSE(fixture.manager.isRestartPending());

    fixture.backend.request("/api/restart", HttpMethod::Post, "admin", "secret");
    TEST_ASSERT_EQUAL_UINT16(202U, fixture.backend.output.status);
    TEST_ASSERT_TRUE(fixture.manager.isRestartPending());
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.restartCalls);
    fixture.runtime.now += 999U;
    fixture.manager.loop();
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.restartCalls);
    fixture.runtime.now += 1U;
    fixture.manager.loop();
    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.restartCalls);
    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.stopCalls);
    TEST_ASSERT_FALSE(fixture.manager.isRestartPending());
    fixture.manager.loop();
    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.restartCalls);
}

void test_missing_admin_password_returns_503() {
    Fixture fixture("");
    fixture.backend.request("/update", HttpMethod::Get, "admin", "");
    TEST_ASSERT_EQUAL_UINT16(503U, fixture.backend.output.status);
}

void test_update_page_auth_and_content() {
    Fixture fixture;
    fixture.backend.request("/update", HttpMethod::Get);
    TEST_ASSERT_EQUAL_UINT16(401U, fixture.backend.output.status);
    fixture.backend.request("/update", HttpMethod::Get, "admin", "wrong");
    TEST_ASSERT_EQUAL_UINT16(401U, fixture.backend.output.status);
    fixture.backend.request("/update", HttpMethod::Get, "admin", "secret");
    TEST_ASSERT_EQUAL_UINT16(200U, fixture.backend.output.status);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(ContentType::Html),
        static_cast<uint8_t>(fixture.backend.output.contentType)
    );
    TEST_ASSERT_NOT_NULL(strstr(fixture.backend.output.body, "X-Firmware-Size"));
    TEST_ASSERT_NOT_NULL(strstr(fixture.backend.output.body, "<progress"));
}

void test_unauthorized_upload_has_no_side_effects() {
    Fixture fixture;
    fixture.upload(WebUploadStatus::Start, "firmware.bin", nullptr, 0U, "3", "wrong");
    const uint8_t bytes[] = {1U, 2U, 3U};
    fixture.upload(WebUploadStatus::Chunk, "firmware.bin", bytes, 3U, "3", "wrong");
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.beginCalls);
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.stopCalls);
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.writeCalls);
    TEST_ASSERT_FALSE(fixture.runtime.maintenance);
}

void test_unauthorized_upload_cannot_interrupt_active_ota() {
    Fixture fixture;
    const uint8_t bytes[] = {1U, 2U, 3U};
    fixture.upload(WebUploadStatus::Start);

    fixture.upload(
        WebUploadStatus::Start, "firmware.bin", nullptr, 0U, "3", "wrong"
    );
    fixture.upload(
        WebUploadStatus::Abort, "firmware.bin", nullptr, 0U, "3", "wrong"
    );

    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.beginCalls);
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.abortCalls);
    TEST_ASSERT_TRUE(fixture.runtime.maintenance);
    TEST_ASSERT_TRUE(fixture.manager.isOtaInProgress());

    fixture.upload(WebUploadStatus::Chunk, "firmware.bin", bytes, 3U);
    fixture.upload(WebUploadStatus::End);
    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.endCalls);
    TEST_ASSERT_TRUE(fixture.manager.isRestartPending());
}

void test_successful_upload_streams_chunks_and_schedules_restart() {
    Fixture fixture;
    const uint8_t first[] = {1U, 2U};
    const uint8_t second[] = {3U};
    fixture.upload(WebUploadStatus::Start);
    fixture.upload(WebUploadStatus::Chunk, "firmware.bin", first, 2U);
    fixture.upload(WebUploadStatus::Chunk, "firmware.bin", second, 1U);
    fixture.upload(WebUploadStatus::End);
    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.beginCalls);
    TEST_ASSERT_EQUAL_UINT32(3U, fixture.runtime.begunSize);
    TEST_ASSERT_EQUAL_UINT16(2U, fixture.runtime.writeCalls);
    TEST_ASSERT_EQUAL_UINT32(3U, fixture.runtime.written);
    TEST_ASSERT_EQUAL_UINT16(2U, fixture.runtime.serviceCalls);
    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.endCalls);
    TEST_ASSERT_TRUE(fixture.runtime.maintenance);
    TEST_ASSERT_TRUE(fixture.manager.isOtaInProgress());
    TEST_ASSERT_TRUE(fixture.manager.isRestartPending());
    fixture.backend.request("/update", HttpMethod::Post, "admin", "secret");
    TEST_ASSERT_EQUAL_UINT16(200U, fixture.backend.output.status);
}

void test_abort_cleans_state_and_maintenance() {
    Fixture fixture;
    fixture.upload(WebUploadStatus::Start);
    fixture.upload(WebUploadStatus::Abort);
    TEST_ASSERT_FALSE(fixture.runtime.maintenance);
    TEST_ASSERT_FALSE(fixture.manager.isOtaInProgress());
    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.abortCalls);
}

void test_invalid_metadata_is_rejected_before_update_begin() {
    Fixture fixture;
    fixture.upload(WebUploadStatus::Start, "firmware.txt");
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.beginCalls);
    fixture.upload(WebUploadStatus::Start, "firmware.bin", nullptr, 0U, nullptr);
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.beginCalls);
    fixture.upload(WebUploadStatus::Start, "firmware.bin", nullptr, 0U, "0");
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.beginCalls);
    fixture.upload(WebUploadStatus::Start, "firmware.bin", nullptr, 0U, "4097");
    TEST_ASSERT_EQUAL_UINT16(0U, fixture.runtime.beginCalls);
    TEST_ASSERT_FALSE(fixture.runtime.maintenance);
}

void test_size_mismatch_cleans_state() {
    Fixture fixture;
    const uint8_t bytes[] = {1U, 2U};
    fixture.upload(WebUploadStatus::Start);
    fixture.upload(WebUploadStatus::Chunk, "firmware.bin", bytes, 2U);
    fixture.upload(WebUploadStatus::End);
    TEST_ASSERT_FALSE(fixture.runtime.maintenance);
    TEST_ASSERT_FALSE(fixture.manager.isOtaInProgress());
    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.abortCalls);
}

void test_update_failures_clean_state() {
    Fixture beginFailure;
    beginFailure.runtime.beginResult = false;
    beginFailure.upload(WebUploadStatus::Start);
    TEST_ASSERT_FALSE(beginFailure.runtime.maintenance);
    TEST_ASSERT_FALSE(beginFailure.manager.isOtaInProgress());

    Fixture writeFailure;
    writeFailure.runtime.writeResult = false;
    const uint8_t bytes[] = {1U, 2U, 3U};
    writeFailure.upload(WebUploadStatus::Start);
    writeFailure.upload(WebUploadStatus::Chunk, "firmware.bin", bytes, 3U);
    TEST_ASSERT_FALSE(writeFailure.runtime.maintenance);
    TEST_ASSERT_EQUAL_UINT16(1U, writeFailure.runtime.abortCalls);

    Fixture endFailure;
    endFailure.runtime.endResult = false;
    endFailure.upload(WebUploadStatus::Start);
    endFailure.upload(WebUploadStatus::Chunk, "firmware.bin", bytes, 3U);
    endFailure.upload(WebUploadStatus::End);
    TEST_ASSERT_FALSE(endFailure.runtime.maintenance);
    TEST_ASSERT_EQUAL_UINT16(1U, endFailure.runtime.abortCalls);
}

void test_next_upload_after_failure_starts_clean() {
    Fixture fixture;
    fixture.runtime.writeResult = false;
    const uint8_t bytes[] = {1U, 2U, 3U};
    fixture.upload(WebUploadStatus::Start);
    fixture.upload(WebUploadStatus::Chunk, "firmware.bin", bytes, 3U);
    fixture.runtime.writeResult = true;
    fixture.upload(WebUploadStatus::Start);
    fixture.upload(WebUploadStatus::Chunk, "firmware.bin", bytes, 3U);
    fixture.upload(WebUploadStatus::End);
    TEST_ASSERT_EQUAL_UINT16(2U, fixture.runtime.beginCalls);
    TEST_ASSERT_EQUAL_UINT16(1U, fixture.runtime.endCalls);
    TEST_ASSERT_TRUE(fixture.manager.isRestartPending());
}

} // namespace

void setUp() {}
void tearDown() {}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_only_legacy_admin_routes_are_registered_by_doser);
    RUN_TEST(test_restart_auth_and_delayed_execution);
    RUN_TEST(test_missing_admin_password_returns_503);
    RUN_TEST(test_update_page_auth_and_content);
    RUN_TEST(test_unauthorized_upload_has_no_side_effects);
    RUN_TEST(test_unauthorized_upload_cannot_interrupt_active_ota);
    RUN_TEST(test_successful_upload_streams_chunks_and_schedules_restart);
    RUN_TEST(test_abort_cleans_state_and_maintenance);
    RUN_TEST(test_invalid_metadata_is_rejected_before_update_begin);
    RUN_TEST(test_size_mismatch_cleans_state);
    RUN_TEST(test_update_failures_clean_state);
    RUN_TEST(test_next_upload_after_failure_starts_clean);
    UNITY_END();
}

void loop() {}
