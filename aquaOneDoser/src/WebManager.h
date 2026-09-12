#pragma once

#include <Arduino.h>
#include <AquaCore/Web/WebTypes.h>

class WebManagerRuntime;

namespace AquaCore {
namespace Web {
class WebService;
}
}

class WebManager {
public:
    bool begin(AquaCore::Web::WebService& webService,
               WebManagerRuntime& runtime,
               const char* adminUser, const char* adminPassword);
    void loop();
    bool isOtaInProgress() const;
    bool isRestartPending() const;

private:
    static constexpr unsigned long RESTART_DELAY_MS = 1000UL;

    WebManagerRuntime* runtime_ = nullptr;
    const char* adminUser_ = nullptr;
    const char* adminPassword_ = nullptr;
    bool routesConfigured = false;
    bool otaInProgress = false;
    bool otaAuthorized = false;
    bool otaAccepted = false;
    bool otaSuccessful = false;
    String otaError;
    size_t expectedFirmwareSize = 0;
    uint8_t nextProgressPercent = 25;
    bool restartPending = false;
    unsigned long restartAt = 0;

    bool configureRoutes(AquaCore::Web::WebService& webService);
    bool authenticateAdmin(const AquaCore::Web::WebRequest& request,
                           AquaCore::Web::WebResponseWriter& response);
    void handleRestart(const AquaCore::Web::WebRequest& request,
                       AquaCore::Web::WebResponseWriter& response);
    void handleUpdatePage(const AquaCore::Web::WebRequest& request,
                          AquaCore::Web::WebResponseWriter& response);
    void handleUpdateResult(const AquaCore::Web::WebRequest& request,
                            AquaCore::Web::WebResponseWriter& response);
    void handleUpload(const AquaCore::Web::WebRequest& request,
                      const AquaCore::Web::WebUploadEvent& event);
    void failOta(const String& message);
    void resetUploadState();
    void scheduleRestart();
    void serviceDuringUpload();

    static void restartRoute(void* context, const AquaCore::Web::WebRequest& request,
                             AquaCore::Web::WebResponseWriter& response);
    static void updatePageRoute(void* context, const AquaCore::Web::WebRequest& request,
                                AquaCore::Web::WebResponseWriter& response);
    static void updateResultRoute(void* context, const AquaCore::Web::WebRequest& request,
                                  AquaCore::Web::WebResponseWriter& response);
    static void uploadRoute(void* context, const AquaCore::Web::WebRequest& request,
                            const AquaCore::Web::WebUploadEvent& event);
};