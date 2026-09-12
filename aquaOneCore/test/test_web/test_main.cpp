#include <Arduino.h>
#include <unity.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "AquaCore/Config/StorageBackend.h"
#include "AquaCore/Config/StorageService.h"
#include "AquaCore/Diagnostics/DiagnosticsService.h"
#include "AquaCore/System/SystemBackend.h"
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Time/RtcBus.h"
#include "AquaCore/Time/RtcService.h"
#include "AquaCore/Version.h"
#include "AquaCore/Web/WebApiProvider.h"
#include "AquaCore/Web/WebBackend.h"
#include "AquaCore/Web/WebConfig.h"
#include "AquaCore/Web/HtmlShell.h"
#include "AquaCore/Web/WebPageProvider.h"
#include "AquaCore/Web/WebService.h"

using namespace AquaCore;
using namespace AquaCore::Config;
using namespace AquaCore::Diagnostics;
using namespace AquaCore::Time;
using namespace AquaCore::Web;

namespace {

class Writer final : public WebResponseWriter {
public:
    bool beginResponse(uint16_t code, ContentType type) override {
        if (begun) return false;
        begun = true; status = code; contentType = type; return true;
    }
    bool write(const char* data, size_t length) override {
        if (!begun || ended || (data == nullptr && length > 0U) ||
            size + length >= sizeof(body)) return false;
        memcpy(body + size, data, length);
        size += length; body[size] = '\0'; return true;
    }
    bool endResponse() override {
        if (!begun || ended) return false;
        ended = true; return true;
    }
    void clear() {
        begun = false; ended = false; status = 0U; size = 0U;
        contentType = ContentType::PlainText; body[0] = '\0';
    }
    bool begun = false;
    bool ended = false;
    uint16_t status = 0U;
    ContentType contentType = ContentType::PlainText;
    char body[3072] {};
    size_t size = 0U;
};

struct RequestHeader {
    const char* name = nullptr;
    const char* value = nullptr;
};

class RequestContext final : public WebRequestContext {
public:
    RequestContext(const RequestHeader* headersValue, size_t headerCountValue,
                   const char* usernameValue, const char* passwordValue,
                   Writer& writerValue)
        : headers(headersValue), headerCount(headerCountValue),
          username(usernameValue), password(passwordValue), writer(writerValue) {}

    bool hasHeader(const char* name) const override {
        if (!name) return false;
        for (size_t i = 0U; i < headerCount; ++i)
            if (headers[i].name && strcmp(headers[i].name, name) == 0)
                return true;
        return false;
    }
    size_t copyHeader(const char* name, char* output,
                      size_t outputSize) const override {
        if (output && outputSize > 0U) output[0] = '\0';
        if (!name) return 0U;
        for (size_t i = 0U; i < headerCount; ++i) {
            if (!headers[i].name || strcmp(headers[i].name, name) != 0)
                continue;
            const char* value = headers[i].value ? headers[i].value : "";
            const size_t length = strlen(value);
            if (output && outputSize > 0U) {
                const size_t copied = length < outputSize - 1U
                    ? length : outputSize - 1U;
                memcpy(output, value, copied); output[copied] = '\0';
            }
            return length;
        }
        return 0U;
    }
    bool authenticateBasic(const char* expectedUsername,
                           const char* expectedPassword) const override {
        return username && password && expectedUsername && expectedPassword &&
            strcmp(username, expectedUsername) == 0 &&
            strcmp(password, expectedPassword) == 0;
    }
    bool requestBasicAuthentication(const char* realm) const override {
        challenged = true;
        strncpy(challengeRealm, realm ? realm : "", sizeof(challengeRealm) - 1U);
        writer.beginResponse(401U, ContentType::PlainText);
        writer.writeText("Unauthorized"); writer.endResponse();
        return true;
    }

    const RequestHeader* headers;
    size_t headerCount;
    const char* username;
    const char* password;
    Writer& writer;
    mutable bool challenged = false;
    mutable char challengeRealm[32] {};
};

class MockBackend final : public WebBackend {
public:
    struct Route {
        char path[64] {};
        HttpMethod method = HttpMethod::Get;
        WebRouteHandler handler = nullptr;
        void* context = nullptr;
        WebRouteOptions options {};
        size_t uploadBytes = 0U;
    };
    bool addRoute(const char* path, HttpMethod method,
                  WebRouteHandler handler, void* context) override {
        if (!path || !handler || count >= 12U || strlen(path) >= 64U)
            return false;
        for (size_t i = 0; i < count; ++i)
            if (routes[i].method == method &&
                strcmp(routes[i].path, path) == 0) return false;
        strcpy(routes[count].path, path);
        routes[count].method = method;
        routes[count].handler = handler;
        routes[count].context = context;
        ++count; return true;
    }
    bool addRoute(const char* path, HttpMethod method,
                  WebRouteHandler handler, void* context,
                  const WebRouteOptions& options) override {
        if ((options.uploadHandler && method != HttpMethod::Post) ||
            (!options.uploadHandler && options.uploadContext)) return false;
        if (!addRoute(path, method, handler, context)) return false;
        routes[count - 1U].options = options; return true;
    }
    bool setNotFoundHandler(WebRouteHandler handler, void* context) override {
        if (!handler) return false;
        notFound = handler; notFoundContext = context; return true;
    }
    bool begin(uint16_t value) override {
        ++beginCalls; port = value; running = beginResult; return running;
    }
    void update() override { ++updateCalls; }
    void stop() override { ++stopCalls; running = false; }
    bool isRunning() const override { return running; }
    bool request(const char* path, HttpMethod method = HttpMethod::Get,
                 const char* body = nullptr,
                 const RequestHeader* headers = nullptr,
                 size_t headerCount = 0U,
                 const char* username = nullptr,
                 const char* password = nullptr) {
        output.clear();
        challenged = false; challengeRealm[0] = '\0';
        const size_t bodyLength = body ? strlen(body) : 0U;
        RequestContext requestContext(
            headers, headerCount, username, password, output
        );
        WebRequest request {method, path, body, bodyLength, &requestContext};
        for (size_t i = 0; i < count; ++i) {
            if (routes[i].method == method &&
                strcmp(routes[i].path, path) == 0) {
                if (routes[i].options.maxBodyLength > 0U &&
                    bodyLength > routes[i].options.maxBodyLength) {
                    output.beginResponse(413U, ContentType::PlainText);
                    output.writeText("Payload Too Large"); output.endResponse();
                    return true;
                }
                routes[i].handler(routes[i].context, request, output);
                challenged = requestContext.challenged;
                strcpy(challengeRealm, requestContext.challengeRealm);
                return true;
            }
        }
        if (!notFound) return false;
        notFound(notFoundContext, request, output); return true;
    }
    bool upload(const char* path, WebUploadStatus status,
                const char* filename = nullptr,
                const uint8_t* data = nullptr, size_t dataLength = 0U) {
        for (size_t i = 0U; i < count; ++i) {
            Route& route = routes[i];
            if (route.method != HttpMethod::Post ||
                strcmp(route.path, path) != 0 ||
                !route.options.uploadHandler) continue;
            if (status == WebUploadStatus::Start) route.uploadBytes = 0U;
            if (status == WebUploadStatus::Chunk) route.uploadBytes += dataLength;
            RequestContext requestContext(nullptr, 0U, nullptr, nullptr, output);
            WebRequest request {
                HttpMethod::Post, path, nullptr, 0U, &requestContext
            };
            WebUploadEvent event {
                status, filename, data, dataLength, route.uploadBytes
            };
            route.options.uploadHandler(
                route.options.uploadContext, request, event
            );
            if (status == WebUploadStatus::End ||
                status == WebUploadStatus::Abort) route.uploadBytes = 0U;
            return true;
        }
        return false;
    }
    Route routes[12] {};
    size_t count = 0U;
    WebRouteHandler notFound = nullptr;
    void* notFoundContext = nullptr;
    Writer output;
    bool challenged = false;
    char challengeRealm[32] {};
    bool beginResult = true;
    bool running = false;
    uint16_t port = 0U, beginCalls = 0U, updateCalls = 0U, stopCalls = 0U;
};

class SysBackend final : public SystemBackend {
public:
    uint32_t uptimeMs() const override { ++uptimeReads; return 1234U; }
    RestartReason restartReason() const override {
        ++reasonReads; return RestartReason::PowerOn;
    }
    mutable uint16_t uptimeReads = 0U, reasonReads = 0U;
};

class RtcBusStub final : public RtcBus {
public:
    bool begin(int, int) override { ++operations; return false; }
    bool readRegisters(uint8_t, uint8_t, uint8_t*, size_t) override {
        ++operations; return false;
    }
    bool writeRegisters(uint8_t, uint8_t, const uint8_t*, size_t) override {
        ++operations; return false;
    }
    uint16_t operations = 0U;
};

class StorageStub final : public StorageBackend {
public:
    bool begin(const char*) override { ++beginCalls; return false; }
    void end() override { ++endCalls; }
    size_t blobLength(const char*) override { ++readCalls; return 0U; }
    size_t readBlob(const char*, void*, size_t) override {
        ++readCalls; return 0U;
    }
    size_t writeBlob(const char*, const void*, size_t) override {
        ++writeCalls; return 0U;
    }
    uint16_t beginCalls = 0U, endCalls = 0U, readCalls = 0U, writeCalls = 0U;
};

struct Fixture {
    Fixture(const char* name = "Aqua Test",
            const char* type = "test-controller")
        : system(sysBackend), rtc(rtcBus, RtcConfig {}),
          storage(storageBackend, "ac7", "a", "b"),
          diagnostics(system, rtc, storage, "Europe/Warsaw"),
          web(backend, system, &diagnostics) {
        TEST_ASSERT_TRUE(system.begin(DeviceIdentity(
            type, name, "7.0.0", "ESP32_TEST"
        )));
    }
    SysBackend sysBackend;
    SystemService system;
    RtcBusStub rtcBus;
    RtcService rtc;
    StorageStub storageBackend;
    StorageService storage;
    DiagnosticsService diagnostics;
    MockBackend backend;
    WebService web;
};

WebConfig config(uint32_t nav = ALL_NAVIGATION_SECTIONS) {
    WebConfig result {};
    result.enabled = true; result.port = 8080U; result.navigationMask = nav;
    return result;
}
void assertContains(const char* body, const char* value) {
    TEST_ASSERT_NOT_NULL(strstr(body, value));
}
void assertMissing(const char* body, const char* value) {
    TEST_ASSERT_NULL(strstr(body, value));
}

struct HandlerState { uint16_t calls = 0U; HttpMethod method = HttpMethod::Get;
                      char body[32] {}; };
void routeHandler(void* value, const WebRequest& request,
                  WebResponseWriter& response) {
    HandlerState* state = static_cast<HandlerState*>(value);
    if (state) {
        ++state->calls; state->method = request.method;
        if (request.body) strncpy(state->body, request.body, 31U);
    }
    response.beginResponse(201U, ContentType::PlainText);
    response.writeText("accepted"); response.endResponse();
}

class Page final : public WebPageProvider {
public:
    const char* route() const override { return "/device"; }
    const char* title() const override { return "Device <view>"; }
    void render(WebResponseWriter& out) const override {
        out.writeText("<section id=\"device-content\">OK</section>");
    }
};
class SystemNavigationPage final : public WebPageProvider {
public:
    const char* route() const override { return "/system"; }
    const char* title() const override { return "System"; }
    void render(WebResponseWriter& out) const override {
        out.writeText("<section id=\"system-content\">OK</section>");
    }
};

class Api final : public WebApiProvider {
public:
    const char* route() const override { return "/api/device"; }
    HttpMethod method() const override { return HttpMethod::Post; }
    void handle(const WebRequest& request, WebResponseWriter& out) override {
        ++calls; length = request.bodyLength;
        out.beginResponse(202U, ContentType::Json);
        out.writeText("{\"accepted\":true}"); out.endResponse();
    }
    uint16_t calls = 0U; size_t length = 0U;
};

struct HeaderState {
    bool present = false;
    bool missing = true;
    bool emptyPresent = false;
    size_t valueLength = 0U;
    char value[32] {};
};
void headerHandler(void* value, const WebRequest& request,
                   WebResponseWriter& response) {
    HeaderState* state = static_cast<HeaderState*>(value);
    state->present = request.hasHeader("X-Test");
    state->missing = request.hasHeader("X-Missing");
    state->emptyPresent = request.hasHeader("X-Empty");
    state->valueLength = request.copyHeader(
        "X-Test", state->value, sizeof(state->value)
    );
    response.beginResponse(200U, ContentType::PlainText); response.endResponse();
}

struct AuthState { uint16_t accepted = 0U; };
void authHandler(void* value, const WebRequest& request,
                 WebResponseWriter& response) {
    AuthState* state = static_cast<AuthState*>(value);
    if (!request.authenticateBasic("admin", "secret")) {
        request.requestBasicAuthentication("Aqua Control"); return;
    }
    ++state->accepted;
    response.beginResponse(204U, ContentType::PlainText); response.endResponse();
}

struct UploadState {
    WebUploadStatus statuses[12] {};
    size_t bytes[12] {};
    size_t lengths[12] {};
    const uint8_t* chunkPointers[12] {};
    char filenames[12][24] {};
    size_t count = 0U;
};
void uploadHandler(void* value, const WebRequest& request,
                   const WebUploadEvent& event) {
    UploadState* state = static_cast<UploadState*>(value);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HttpMethod::Post),
        static_cast<uint8_t>(request.method)
    );
    TEST_ASSERT_LESS_THAN_UINT32(12U, state->count);
    const size_t index = state->count++;
    state->statuses[index] = event.status;
    state->bytes[index] = event.bytesReceived;
    state->lengths[index] = event.dataLength;
    state->chunkPointers[index] = event.data;
    if (event.filename)
        strncpy(state->filenames[index], event.filename, 23U);
}
void uploadCompletionHandler(void*, const WebRequest&,
                             WebResponseWriter& response) {
    response.beginResponse(200U, ContentType::PlainText);
    response.writeText("done"); response.endResponse();
}
WebRouteOptions uploadOptions(UploadState& state) {
    WebRouteOptions options {};
    options.uploadHandler = uploadHandler;
    options.uploadContext = &state;
    return options;
}

void test_disabled() {
    Fixture f; WebConfig c {}; TEST_ASSERT_TRUE(f.web.begin(c));
    TEST_ASSERT_FALSE(f.web.isRunning()); TEST_ASSERT_EQUAL_UINT16(0, f.backend.beginCalls);
}
void test_begin() {
    Fixture f; TEST_ASSERT_TRUE(f.web.begin(config()));
    TEST_ASSERT_TRUE(f.web.isRunning()); TEST_ASSERT_EQUAL_UINT16(8080, f.backend.port);
}
void test_stop() {
    Fixture f; TEST_ASSERT_TRUE(f.web.begin(config())); f.web.stop();
    TEST_ASSERT_FALSE(f.web.isRunning()); TEST_ASSERT_EQUAL_UINT16(1, f.backend.stopCalls);
}
void test_update() {
    Fixture f; TEST_ASSERT_TRUE(f.web.begin(config())); f.web.update();
    TEST_ASSERT_EQUAL_UINT16(1, f.backend.updateCalls);
}
void test_no_update_when_stopped() {
    Fixture f; f.web.update(); TEST_ASSERT_EQUAL_UINT16(0, f.backend.updateCalls);
}
void test_get_route() {
    Fixture f; HandlerState s;
    TEST_ASSERT_TRUE(f.web.addRoute("/custom", HttpMethod::Get, routeHandler, &s));
    TEST_ASSERT_TRUE(f.web.begin(config())); f.backend.request("/custom");
    TEST_ASSERT_EQUAL_UINT16(1, s.calls); TEST_ASSERT_EQUAL_UINT16(201, f.backend.output.status);
}
void test_post_route() {
    Fixture f; HandlerState s;
    TEST_ASSERT_TRUE(f.web.addRoute("/post", HttpMethod::Post, routeHandler, &s));
    TEST_ASSERT_TRUE(f.web.begin(config()));
    f.backend.request("/post", HttpMethod::Post, "{\"x\":1}");
    TEST_ASSERT_EQUAL_STRING("{\"x\":1}", s.body);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HttpMethod::Post, (uint8_t)s.method);
}
void test_404() {
    Fixture f; TEST_ASSERT_TRUE(f.web.begin(config())); f.backend.request("/none");
    TEST_ASSERT_EQUAL_UINT16(404, f.backend.output.status);
    TEST_ASSERT_EQUAL_STRING("Not Found", f.backend.output.body);
}
void test_html_type() {
    Fixture f; f.web.begin(config()); f.backend.request("/");
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ContentType::Html, (uint8_t)f.backend.output.contentType);
}
void test_json_type() {
    Fixture f; f.web.begin(config()); f.backend.request("/api/system");
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ContentType::Json, (uint8_t)f.backend.output.contentType);
}
void test_plain_type() {
    Fixture f; f.web.begin(config()); f.backend.request("/none");
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ContentType::PlainText, (uint8_t)f.backend.output.contentType);
}
void test_css_asset() {
    Fixture f; f.web.begin(config()); f.backend.request("/assets/aqua.css");
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ContentType::Css, (uint8_t)f.backend.output.contentType);
    assertContains(f.backend.output.body, "color-scheme:dark");
    assertMissing(f.backend.output.body, "https://");
}
void test_yopilot_theme_and_mobile_contract() {
    const char* css = HtmlShell::stylesheet();
    assertContains(css, "--bg:#101827");
    assertContains(css, "--surface:#1f2937");
    assertContains(css, "--surface-alt:#111827");
    assertContains(css, "--text:#e5e7eb");
    assertContains(css, "--text-muted:#9ca3af");
    assertContains(css, "--accent:#65d46e");
    assertContains(css, "--border:#374151");
    assertContains(css, "width:min(720px");
    assertContains(css, "font-size:22px");
    assertContains(css, "border-radius:12px;padding:12px;margin:8px");
    assertContains(css, "min-height:36px");
    assertContains(css, "width:calc(100% - 16px)");
    assertContains(css, "@media(max-width:460px)");
    assertMissing(css, "https://");
}
void test_current_navigation_item_is_active() {
    Fixture f;
    SystemNavigationPage page;
    TEST_ASSERT_TRUE(f.web.addPage(page));
    TEST_ASSERT_TRUE(f.web.begin(config(
        navigationSectionMask(NavigationSection::System)
    )));
    f.backend.request("/system");
    assertContains(
        f.backend.output.body,
        "<a class=\"active\" aria-current=\"page\" href=\"/system\">"
    );
    assertContains(f.backend.output.body, "id=\"system-content\"");
}

void test_system_identity() {
    Fixture f("AquaDoser", "dosing-controller"); f.web.begin(config());
    f.backend.request("/api/system");
    assertContains(f.backend.output.body, "\"deviceName\":\"AquaDoser\"");
    assertContains(f.backend.output.body, "\"deviceType\":\"dosing-controller\"");
    assertContains(f.backend.output.body, "\"hardwareVariant\":\"ESP32_TEST\"");
}
void test_system_version() {
    Fixture f; f.web.begin(config()); f.backend.request("/api/system");
    assertContains(f.backend.output.body, AQUA_CORE_VERSION);
}
void test_diagnostics() {
    Fixture f; f.web.begin(config()); f.backend.request("/api/diagnostics");
    TEST_ASSERT_EQUAL_UINT16(200, f.backend.output.status);
    assertContains(f.backend.output.body, "\"health\":");
    assertContains(f.backend.output.body, "\"time\":");
    assertContains(f.backend.output.body, "\"storage\":");
}
void test_diagnostics_without_network() {
    Fixture f; f.web.begin(config()); f.backend.request("/api/diagnostics");
    assertContains(f.backend.output.body,
        "\"network\":{\"health\":\"unknown\",\"available\":false");
}
void test_no_wifi_password() {
    Fixture f; f.web.begin(config()); f.backend.request("/api/diagnostics");
    assertMissing(f.backend.output.body, "wifi-secret-ac7");
    assertMissing(f.backend.output.body, "\"password\"");
}
void test_no_ap_password() {
    Fixture f; f.web.begin(config()); f.backend.request("/api/diagnostics");
    assertMissing(f.backend.output.body, "ap-secret-ac7");
    assertMissing(f.backend.output.body, "apPassword");
}
void test_shell_device_name() {
    Fixture f("HydroSense"); f.web.begin(config()); f.backend.request("/");
    assertContains(f.backend.output.body, "<h1>HydroSense</h1>");
}
void test_no_hardcoded_lumasense() {
    Fixture f("AquaFish"); f.web.begin(config()); f.backend.request("/");
    assertMissing(f.backend.output.body, "LumaSense");
}
void test_navigation_selection() {
    Fixture* first = new Fixture();
    TEST_ASSERT_NOT_NULL(first);
    first->web.begin(config(navigationSectionMask(
        NavigationSection::Dashboard)));
    first->backend.request("/");
    assertContains(first->backend.output.body, "href=\"/\"");
    assertMissing(first->backend.output.body, "href=\"/system\"");
    delete first;

    Fixture* second = new Fixture();
    TEST_ASSERT_NOT_NULL(second);
    second->web.begin(config(navigationSectionMask(
        NavigationSection::System)));
    second->backend.request("/");
    assertMissing(second->backend.output.body, "href=\"/\"");
    assertContains(second->backend.output.body, "href=\"/system\"");
    delete second;
}
void test_page_provider() {
    Fixture f; Page page; TEST_ASSERT_TRUE(f.web.addPage(page));
    f.web.begin(config()); f.backend.request("/device");
    assertContains(f.backend.output.body, "id=\"device-content\"");
    assertContains(f.backend.output.body, "Device &lt;view&gt;");
}
void test_api_provider() {
    Fixture f; Api api; TEST_ASSERT_TRUE(f.web.addApi(api)); f.web.begin(config());
    f.backend.request("/api/device", HttpMethod::Post, "abc");
    TEST_ASSERT_EQUAL_UINT16(1, api.calls); TEST_ASSERT_EQUAL_UINT32(3, api.length);
    TEST_ASSERT_EQUAL_UINT16(202, f.backend.output.status);
}
void test_two_services() {
    MockBackend* a = new MockBackend(); MockBackend* b = new MockBackend();
    TEST_ASSERT_NOT_NULL(a); TEST_ASSERT_NOT_NULL(b);
    WebService first(*a), second(*b);
    TEST_ASSERT_TRUE(first.begin(config())); TEST_ASSERT_TRUE(second.begin(config()));
    first.stop(); TEST_ASSERT_FALSE(first.isRunning()); TEST_ASSERT_TRUE(second.isRunning());
    delete a; delete b;
}
void test_independent_state() {
    Fixture* first = new Fixture("First");
    TEST_ASSERT_NOT_NULL(first);
    first->web.begin(config());
    first->backend.request("/");
    assertContains(first->backend.output.body, "First");
    assertMissing(first->backend.output.body, "Second");
    delete first;

    Fixture* second = new Fixture("Second");
    TEST_ASSERT_NOT_NULL(second);
    second->web.begin(config());
    second->backend.request("/");
    assertContains(second->backend.output.body, "Second");
    assertMissing(second->backend.output.body, "First");
    delete second;
}
void test_null_callback() {
    Fixture f; TEST_ASSERT_FALSE(f.web.addRoute(
        "/bad", HttpMethod::Get, nullptr));
    f.web.begin(config()); f.backend.request("/bad");
    TEST_ASSERT_EQUAL_UINT16(404, f.backend.output.status);
}
void test_invalid_path() {
    Fixture f; HandlerState s;
    TEST_ASSERT_FALSE(f.web.addRoute(nullptr, HttpMethod::Get, routeHandler, &s));
    TEST_ASSERT_FALSE(f.web.addRoute("bad", HttpMethod::Get, routeHandler, &s));
}
void test_html_escape() {
    Fixture f("Aqua <Lab> & \"Fish\""); f.web.begin(config()); f.backend.request("/");
    assertContains(f.backend.output.body,
        "Aqua &lt;Lab&gt; &amp; &quot;Fish&quot;");
    assertMissing(f.backend.output.body, "<h1>Aqua <Lab>");
}
void test_json_escape() {
    Fixture f("Aqua \"Lab\"\nBack\\slash"); f.web.begin(config());
    f.backend.request("/api/system");
    assertContains(f.backend.output.body, "Aqua \\\"Lab\\\"\\nBack\\\\slash");
}
void test_no_network_control() {
    Fixture f; volatile uint16_t connectCalls = 0, reconnectCalls = 0;
    f.web.begin(config()); f.web.update();
    TEST_ASSERT_EQUAL_UINT16(0, connectCalls);
    TEST_ASSERT_EQUAL_UINT16(0, reconnectCalls);
}
void test_no_storage_write() {
    Fixture f; uint16_t before = f.storageBackend.writeCalls;
    f.web.begin(config()); f.backend.request("/api/diagnostics");
    TEST_ASSERT_EQUAL_UINT16(before, f.storageBackend.writeCalls);
}
void test_no_system_restart() {
    Fixture f; RestartReason before = f.system.restartReason();
    f.web.begin(config()); f.backend.request("/api/system");
    TEST_ASSERT_EQUAL_UINT8((uint8_t)before, (uint8_t)f.system.restartReason());
    TEST_ASSERT_TRUE(f.system.isReady());
}
void test_begin_failure() {
    Fixture f; f.backend.beginResult = false;
    TEST_ASSERT_FALSE(f.web.begin(config())); TEST_ASSERT_FALSE(f.web.isRunning());
}
void test_invalid_config() {
    Fixture f; WebConfig c = config(); c.port = 0U;
    TEST_ASSERT_FALSE(f.web.begin(c)); c = config();
    c.navigationMask |= 1UL << 20U; TEST_ASSERT_FALSE(f.web.begin(c));
}
void test_backend_only_service() {
    MockBackend b; WebService web(b); TEST_ASSERT_TRUE(web.begin(config()));
    b.request("/"); assertContains(b.output.body, "Aqua Device");
    b.request("/api/system"); TEST_ASSERT_EQUAL_UINT16(503, b.output.status);
}
void test_content_type_names() {
    TEST_ASSERT_EQUAL_STRING("text/html; charset=utf-8",
        contentTypeName(ContentType::Html));
    TEST_ASSERT_EQUAL_STRING("application/json; charset=utf-8",
        contentTypeName(ContentType::Json));
    TEST_ASSERT_EQUAL_STRING("text/plain; charset=utf-8",
        contentTypeName(ContentType::PlainText));
}

void test_headers_existing_missing_and_empty() {
    Fixture f; HeaderState state;
    TEST_ASSERT_TRUE(f.web.addRoute(
        "/headers", HttpMethod::Get, headerHandler, &state
    ));
    RequestHeader headers[] = {{"X-Test", "value"}, {"X-Empty", ""}};
    f.backend.request("/headers", HttpMethod::Get, nullptr, headers, 2U);
    TEST_ASSERT_TRUE(state.present); TEST_ASSERT_FALSE(state.missing);
    TEST_ASSERT_TRUE(state.emptyPresent);
    TEST_ASSERT_EQUAL_UINT32(5U, state.valueLength);
    TEST_ASSERT_EQUAL_STRING("value", state.value);
}
void test_header_value_is_copied_during_request() {
    Fixture f; HeaderState state; char source[] = "before";
    TEST_ASSERT_TRUE(f.web.addRoute(
        "/headers", HttpMethod::Get, headerHandler, &state
    ));
    RequestHeader header {"X-Test", source};
    f.backend.request("/headers", HttpMethod::Get, nullptr, &header, 1U);
    strcpy(source, "after");
    TEST_ASSERT_EQUAL_STRING("before", state.value);
}
void test_auth_correct_credentials() {
    Fixture f; AuthState state;
    TEST_ASSERT_TRUE(f.web.addRoute(
        "/private", HttpMethod::Get, authHandler, &state
    ));
    f.backend.request(
        "/private", HttpMethod::Get, nullptr, nullptr, 0U,
        "admin", "secret"
    );
    TEST_ASSERT_EQUAL_UINT16(1U, state.accepted);
    TEST_ASSERT_EQUAL_UINT16(204U, f.backend.output.status);
    TEST_ASSERT_FALSE(f.backend.challenged);
}
void test_auth_wrong_credentials_challenge() {
    Fixture f; AuthState state;
    TEST_ASSERT_TRUE(f.web.addRoute(
        "/private", HttpMethod::Get, authHandler, &state
    ));
    f.backend.request(
        "/private", HttpMethod::Get, nullptr, nullptr, 0U,
        "admin", "wrong"
    );
    TEST_ASSERT_EQUAL_UINT16(0U, state.accepted);
    TEST_ASSERT_EQUAL_UINT16(401U, f.backend.output.status);
    TEST_ASSERT_TRUE(f.backend.challenged);
    TEST_ASSERT_EQUAL_STRING("Aqua Control", f.backend.challengeRealm);
}
void test_auth_missing_credentials_challenge() {
    Fixture f; AuthState state;
    TEST_ASSERT_TRUE(f.web.addRoute(
        "/private", HttpMethod::Get, authHandler, &state
    ));
    f.backend.request("/private");
    TEST_ASSERT_EQUAL_UINT16(0U, state.accepted);
    TEST_ASSERT_EQUAL_UINT16(401U, f.backend.output.status);
    TEST_ASSERT_TRUE(f.backend.challenged);
}
void test_auth_does_not_affect_public_route() {
    Fixture f; HandlerState state;
    TEST_ASSERT_TRUE(f.web.addRoute(
        "/public", HttpMethod::Get, routeHandler, &state
    ));
    f.backend.request("/public");
    TEST_ASSERT_EQUAL_UINT16(1U, state.calls);
    TEST_ASSERT_EQUAL_UINT16(201U, f.backend.output.status);
}
void test_body_below_and_exact_limit() {
    Fixture f; HandlerState state; WebRouteOptions options {};
    options.maxBodyLength = 4U;
    TEST_ASSERT_TRUE(f.web.addRoute(
        "/limited", HttpMethod::Post, routeHandler, &state, options
    ));
    f.backend.request("/limited", HttpMethod::Post, "abc");
    f.backend.request("/limited", HttpMethod::Post, "abcd");
    TEST_ASSERT_EQUAL_UINT16(2U, state.calls);
    TEST_ASSERT_EQUAL_UINT16(201U, f.backend.output.status);
}
void test_body_above_limit_returns_413() {
    Fixture f; HandlerState state; WebRouteOptions options {};
    options.maxBodyLength = 4U;
    TEST_ASSERT_TRUE(f.web.addRoute(
        "/limited", HttpMethod::Post, routeHandler, &state, options
    ));
    f.backend.request("/limited", HttpMethod::Post, "abcde");
    TEST_ASSERT_EQUAL_UINT16(0U, state.calls);
    TEST_ASSERT_EQUAL_UINT16(413U, f.backend.output.status);
}
void test_empty_body_with_limit() {
    Fixture f; HandlerState state; WebRouteOptions options {};
    options.maxBodyLength = 4U;
    TEST_ASSERT_TRUE(f.web.addRoute(
        "/limited", HttpMethod::Post, routeHandler, &state, options
    ));
    f.backend.request("/limited", HttpMethod::Post);
    TEST_ASSERT_EQUAL_UINT16(1U, state.calls);
}
void test_upload_start_chunks_end_and_filename() {
    MockBackend backend; WebService web(backend); UploadState state;
    TEST_ASSERT_TRUE(web.addRoute(
        "/upload", HttpMethod::Post, uploadCompletionHandler, nullptr,
        uploadOptions(state)
    ));
    const uint8_t first[] = {1U, 2U}; const uint8_t second[] = {3U};
    backend.upload("/upload", WebUploadStatus::Start, "image.bin");
    backend.upload("/upload", WebUploadStatus::Chunk, "image.bin", first, 2U);
    backend.upload("/upload", WebUploadStatus::Chunk, "image.bin", second, 1U);
    backend.upload("/upload", WebUploadStatus::End, "image.bin");
    TEST_ASSERT_EQUAL_UINT32(4U, state.count);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WebUploadStatus::Start,
                            (uint8_t)state.statuses[0]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WebUploadStatus::Chunk,
                            (uint8_t)state.statuses[1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WebUploadStatus::End,
                            (uint8_t)state.statuses[3]);
    TEST_ASSERT_EQUAL_UINT32(2U, state.bytes[1]);
    TEST_ASSERT_EQUAL_UINT32(3U, state.bytes[2]);
    TEST_ASSERT_EQUAL_UINT32(3U, state.bytes[3]);
    TEST_ASSERT_EQUAL_STRING("image.bin", state.filenames[0]);
    TEST_ASSERT_EQUAL_PTR(first, state.chunkPointers[1]);
    TEST_ASSERT_EQUAL_PTR(second, state.chunkPointers[2]);
}
void test_upload_abort_and_cleanup() {
    MockBackend backend; WebService web(backend); UploadState state;
    TEST_ASSERT_TRUE(web.addRoute(
        "/upload", HttpMethod::Post, uploadCompletionHandler, nullptr,
        uploadOptions(state)
    ));
    const uint8_t bytes[] = {1U, 2U, 3U};
    backend.upload("/upload", WebUploadStatus::Start, "first.bin");
    backend.upload("/upload", WebUploadStatus::Chunk, "first.bin", bytes, 3U);
    backend.upload("/upload", WebUploadStatus::Abort, "first.bin");
    backend.upload("/upload", WebUploadStatus::Start, "second.bin");
    backend.upload("/upload", WebUploadStatus::Chunk, "second.bin", bytes, 1U);
    backend.upload("/upload", WebUploadStatus::End, "second.bin");
    TEST_ASSERT_EQUAL_UINT8((uint8_t)WebUploadStatus::Abort,
                            (uint8_t)state.statuses[2]);
    TEST_ASSERT_EQUAL_UINT32(3U, state.bytes[2]);
    TEST_ASSERT_EQUAL_UINT32(0U, state.bytes[3]);
    TEST_ASSERT_EQUAL_UINT32(1U, state.bytes[4]);
    TEST_ASSERT_EQUAL_STRING("second.bin", state.filenames[3]);
}
void test_zero_byte_and_independent_uploads() {
    MockBackend backend; WebService web(backend); UploadState state;
    TEST_ASSERT_TRUE(web.addRoute(
        "/upload", HttpMethod::Post, uploadCompletionHandler, nullptr,
        uploadOptions(state)
    ));
    backend.upload("/upload", WebUploadStatus::Start, "empty.bin");
    backend.upload("/upload", WebUploadStatus::End, "empty.bin");
    backend.upload("/upload", WebUploadStatus::Start, "next.bin");
    backend.upload("/upload", WebUploadStatus::End, "next.bin");
    TEST_ASSERT_EQUAL_UINT32(4U, state.count);
    TEST_ASSERT_EQUAL_UINT32(0U, state.bytes[1]);
    TEST_ASSERT_EQUAL_UINT32(0U, state.bytes[2]);
    TEST_ASSERT_EQUAL_STRING("empty.bin", state.filenames[0]);
    TEST_ASSERT_EQUAL_STRING("next.bin", state.filenames[2]);
}
void test_upload_requires_post_route() {
    Fixture f; UploadState state;
    TEST_ASSERT_FALSE(f.web.addRoute(
        "/upload", HttpMethod::Get, routeHandler, nullptr,
        uploadOptions(state)
    ));
}

} // namespace

void setUp() {}
void tearDown() {}

void setup() {
    delay(2000); UNITY_BEGIN();
    RUN_TEST(test_disabled);
    RUN_TEST(test_begin);
    RUN_TEST(test_stop);
    RUN_TEST(test_update);
    RUN_TEST(test_no_update_when_stopped);
    RUN_TEST(test_get_route);
    RUN_TEST(test_post_route);
    RUN_TEST(test_404);
    RUN_TEST(test_html_type);
    RUN_TEST(test_json_type);
    RUN_TEST(test_plain_type);
    RUN_TEST(test_css_asset);
    RUN_TEST(test_yopilot_theme_and_mobile_contract);
    RUN_TEST(test_current_navigation_item_is_active);
    RUN_TEST(test_system_identity);
    RUN_TEST(test_system_version);
    RUN_TEST(test_diagnostics);
    RUN_TEST(test_diagnostics_without_network);
    RUN_TEST(test_no_wifi_password);
    RUN_TEST(test_no_ap_password);
    RUN_TEST(test_shell_device_name);
    RUN_TEST(test_no_hardcoded_lumasense);
    RUN_TEST(test_navigation_selection);
    RUN_TEST(test_page_provider);
    RUN_TEST(test_api_provider);
    RUN_TEST(test_two_services);
    RUN_TEST(test_independent_state);
    RUN_TEST(test_null_callback);
    RUN_TEST(test_invalid_path);
    RUN_TEST(test_html_escape);
    RUN_TEST(test_json_escape);
    RUN_TEST(test_no_network_control);
    RUN_TEST(test_no_storage_write);
    RUN_TEST(test_no_system_restart);
    RUN_TEST(test_begin_failure);
    RUN_TEST(test_invalid_config);
    RUN_TEST(test_backend_only_service);
    RUN_TEST(test_content_type_names);
    RUN_TEST(test_headers_existing_missing_and_empty);
    RUN_TEST(test_header_value_is_copied_during_request);
    RUN_TEST(test_auth_correct_credentials);
    RUN_TEST(test_auth_wrong_credentials_challenge);
    RUN_TEST(test_auth_missing_credentials_challenge);
    RUN_TEST(test_auth_does_not_affect_public_route);
    RUN_TEST(test_body_below_and_exact_limit);
    RUN_TEST(test_body_above_limit_returns_413);
    RUN_TEST(test_empty_body_with_limit);
    RUN_TEST(test_upload_start_chunks_end_and_filename);
    RUN_TEST(test_upload_abort_and_cleanup);
    RUN_TEST(test_zero_byte_and_independent_uploads);
    RUN_TEST(test_upload_requires_post_route);
    UNITY_END();
}
void loop() {}
