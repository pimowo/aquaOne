#pragma once

#include <atomic>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "AquaCore/Web/HttpRouteRegistry.h"
#include "AquaCore/Web/HttpServerTransport.h"
#include "AquaCore/Web/Realtime.h"
#include "AquaCore/Web/RealtimeResync.h"

namespace AquaCore {
namespace Web {

enum class RealtimeRecoveryServiceResult : uint8_t {
    Idle,
    Progress,
    RetryNeeded,
    TransportRecycleSuggested
};

// Application-owned physical HTTPD transport. Route callbacks run in the
// HTTPD server task, never implicitly in the Application loop. F9.2 routes
// must not read mutable Domain state or invoke Domain/hardware actions.
// Lifecycle methods and destruction belong to the Application/composition
// context, never to a route callback or the HTTPD server task: httpd_stop()
// waits for that task to exit. Calls from the owner are serialized.
class EspIdfWebTransport final : public HttpServerTransport {
public:
    EspIdfWebTransport();
    ~EspIdfWebTransport();
    EspIdfWebTransport(const EspIdfWebTransport&) = delete;
    EspIdfWebTransport& operator=(const EspIdfWebTransport&) = delete;

    bool addRoute(const char* path, HttpMethod method,
                  HttpRouteHandler handler, void* context = nullptr) override;
    bool setNotFoundHandler(HttpNotFoundHandler handler,
                            void* context = nullptr) override;
    bool setRealtimeEndpoint(const char* path);
    RealtimePublicationResult publishRealtime(
        const RealtimeNotificationMetadata& metadata, RealtimeFrameType type,
        const uint8_t* payload, size_t length
    );
    bool publishRealtimeStreamStartState(const RealtimeStreamStartState& state);
    void requestRealtimeRecovery();
    bool isRealtimeRecoveryRequired() const {
        return realtimeRecovery_.isRequired();
    }
    RealtimeRecoveryServiceResult serviceRealtimeRecovery();
    bool tryClearRealtimeRecovery(Identity::RuntimeIdentity runtime,
                                  RealtimeStreamPosition coherentPosition);

    // Same-port repeat is idempotent. Different-port repeat fails. Failed
    // starts leave no running server; stop allows retry with frozen routes.
    bool begin(uint16_t port) override;
    void stop() override;
    bool isRunning() const override { return accepting_.load(); }

private:
    static esp_err_t dispatch(httpd_req_t* request);
    static esp_err_t notFound(httpd_req_t* request, httpd_err_code_t error);
    static esp_err_t realtimeDispatch(httpd_req_t* request);
    static void realtimeWork(void* argument);
    static void streamStartWork(void* argument);
    static void keepBorrowedContext(void*);

    HttpRouteRegistry routes_;
    static constexpr size_t REALTIME_PATH_CAPACITY = HttpRouteRegistry::MAX_PATH_LENGTH;
    static constexpr size_t REALTIME_WORK_CAPACITY = 4U;
    static constexpr size_t REALTIME_PAYLOAD_CAPACITY = 256U;
    // HTTPD_DEFAULT_CONFIG() uses seven sockets on the supported ESP-IDF
    // baseline. The stream sends to at most that fixed set per work item.
    static constexpr size_t REALTIME_CLIENT_CAPACITY = 7U;
    struct RealtimeWork {
        EspIdfWebTransport* owner = nullptr;
        RealtimeNotificationMetadata metadata {};
        uint8_t payload[REALTIME_PAYLOAD_CAPACITY] {};
        size_t length = 0U;
        RealtimeFrameType type = RealtimeFrameType::Text;
        RealtimeRecoveryState::Generation recoveryGeneration = 0U;
        std::atomic<bool> inUse {false};
    };
    struct StreamStartWork {
        EspIdfWebTransport* owner = nullptr;
        RealtimeClientToken token {};
        RealtimeStreamStartState state {};
        RealtimeRecoveryState::Generation recoveryGeneration = 0U;
        uint8_t payload[18U] {};
        size_t length = 0U;
        std::atomic<bool> inUse {false};
    };
    char realtimePath_[REALTIME_PATH_CAPACITY + 1U] {};
    bool realtimeConfigured_ = false;
    RealtimeWork realtimeWork_[REALTIME_WORK_CAPACITY];
    StreamStartWork streamStartWork_[REALTIME_CLIENT_CAPACITY];
    RealtimeClientRegistry<REALTIME_CLIENT_CAPACITY> realtimeClients_;
    RealtimeStreamStartState streamStartState_;
    RealtimeRecoveryState realtimeRecovery_;
    httpd_handle_t server_ = nullptr;
    uint16_t port_ = 0U;
    std::atomic<bool> accepting_ {false};
    StaticSemaphore_t realtimeMutexStorage_ {};
    SemaphoreHandle_t realtimeMutex_ = nullptr;
};

} // namespace Web
} // namespace AquaCore
