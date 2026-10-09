#pragma once

#include <AquaCore/Web/NativeWebService.h>
#include <AquaCore/Web/WebApplicationBridge.h>

enum class DoserWebRequestKind : uint8_t { ScheduleRestart };
struct DoserWebRequest { DoserWebRequestKind kind; };
enum class DoserWebResult : uint8_t { Scheduled, Rejected };
using DoserWebBridge = AquaCore::Web::WebApplicationBridge<
    DoserWebRequest, DoserWebResult, 4U>;
static_assert(std::is_trivially_copyable<DoserWebRequest>::value,
              "Doser request must be a bounded copy");

class DoserRestartAuthority {
public:
    virtual ~DoserRestartAuthority() = default;
    virtual uint32_t nowMs() const = 0;
    virtual void stopPumps() = 0;
    virtual void restartDevice() = 0;
};

class DoserWebApplication {
public:
    DoserWebApplication(DoserWebBridge& bridge, DoserRestartAuthority& authority)
        : bridge_(bridge), authority_(authority) {}
    bool processOne();
    void serviceRestart();
    bool restartPending() const { return restartPending_; }
    uint32_t restartAt() const { return restartAt_; }

private:
    static DoserWebResult execute(const DoserWebRequest& request, void* context);
    DoserWebBridge& bridge_;
    DoserRestartAuthority& authority_;
    bool restartPending_ = false;
    uint32_t restartAt_ = 0U;
};

class DoserNativeWebRoutes {
public:
    // Credentials are borrowed for callback use. No request context crosses
    // the bridge; the accepted typed request contains one enum only.
    DoserNativeWebRoutes(DoserWebBridge& bridge, const char* user,
                         const char* password)
        : bridge_(bridge), user_(user), password_(password) {}
    bool addTo(AquaCore::Web::NativeWebService& service);

private:
    bool authenticateAdmin(const AquaCore::Web::HttpRouteRequest& request,
                           AquaCore::Web::WebResponseWriter& response) const;
    static void restartRoute(void*, const AquaCore::Web::HttpRouteRequest&,
                             AquaCore::Web::WebResponseWriter&);
    static void updateRoute(void*, const AquaCore::Web::HttpRouteRequest&,
                            AquaCore::Web::WebResponseWriter&);
    DoserWebBridge& bridge_;
    const char* user_;
    const char* password_;
};
