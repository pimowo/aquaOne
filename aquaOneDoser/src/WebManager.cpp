#include "WebManager.h"

#include "DoserWebRuntime.h"

#include <AquaCore/Web/WebService.h>
#include <Arduino.h>
#include <cstring>

using AquaCore::Web::ContentType;
using AquaCore::Web::HttpMethod;
using AquaCore::Web::WebRequest;
using AquaCore::Web::WebResponseWriter;
using AquaCore::Web::WebRouteOptions;
using AquaCore::Web::WebService;
using AquaCore::Web::WebUploadEvent;
using AquaCore::Web::WebUploadStatus;

namespace {
const char UPDATE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Aktualizacja PMW AquaDoser</title><style>body{font-family:system-ui;background:#eef4f7;padding:20px}.box{max-width:560px;margin:auto;background:#fff;padding:22px;border-radius:12px}button{padding:11px 16px;background:#126b86;color:#fff;border:0;border-radius:8px;font-weight:700}progress{width:100%;height:22px;margin-top:14px}</style></head><body><div class="box"><h1>PMW AquaDoser — Aktualizacja firmware</h1><p>Wybierz plik <code>firmware.bin</code> wygenerowany przez PlatformIO.</p><form id="f"><input id="file" type="file" accept=".bin,application/octet-stream" required><button>AKTUALIZUJ</button></form><progress id="p" value="0" max="100"></progress><p id="m"></p><p><a href="/">Powrót</a></p></div><script>document.getElementById('f').onsubmit=function(e){e.preventDefault();let file=document.getElementById('file').files[0];if(!file||!file.name.toLowerCase().endsWith('.bin')){m.textContent='Wybierz plik .bin';return}if(!confirm('Rozpocząć aktualizację firmware?'))return;let x=new XMLHttpRequest(),fd=new FormData();fd.append('firmware',file);x.open('POST','/update');x.setRequestHeader('X-Firmware-Size',file.size);x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded*100/e.total};x.onload=()=>{m.textContent=x.responseText};x.onerror=()=>{m.textContent='Błąd połączenia podczas aktualizacji'};m.textContent='Wysyłanie...';x.send(fd)};</script></body></html>
)HTML";

void sendText(WebResponseWriter& response, uint16_t status, const char* text) {
    response.beginResponse(status, ContentType::PlainText);
    response.writeText(text);
    response.endResponse();
}
}

bool WebManager::begin(WebService& webService,
                       WebManagerRuntime& runtime,
                       const char* adminUser, const char* adminPassword) {
    runtime_ = &runtime;
    adminUser_ = adminUser;
    adminPassword_ = adminPassword;
    return configureRoutes(webService);
}

bool WebManager::configureRoutes(WebService& webService) {
    if (routesConfigured) return true;
    WebRouteOptions uploadOptions {};
    uploadOptions.uploadHandler = uploadRoute;
    uploadOptions.uploadContext = this;
    if (!webService.addRoute("/api/restart", HttpMethod::Post, restartRoute, this) ||
        !webService.addRoute("/update", HttpMethod::Get, updatePageRoute, this) ||
        !webService.addRoute("/update", HttpMethod::Post, updateResultRoute, this,
                             uploadOptions)) {
        return false;
    }
    routesConfigured = true;
    return true;
}

void WebManager::loop() {
    if (runtime_ != nullptr && restartPending &&
        static_cast<long>(runtime_->nowMs() - restartAt) >= 0) {
        restartPending = false;
        Serial.println("[WEB] Restarting device");
        runtime_->stopPumps();
        runtime_->restartDevice();
    }
}

bool WebManager::authenticateAdmin(const WebRequest& request,
                                   WebResponseWriter& response) {
    if (adminPassword_ == nullptr ||
        strcmp(adminPassword_, "CHANGE_ME_BEFORE_USE") == 0 ||
        adminPassword_[0] == '\0') {
        sendText(response, 503U,
                 "Funkcje administracyjne są wyłączone. Ustaw WEB_PASS w secrets.h.");
        return false;
    }
    if (request.authenticateBasic(adminUser_, adminPassword_)) return true;
    request.requestBasicAuthentication("PMW AquaDoser");
    return false;
}

void WebManager::handleRestart(const WebRequest& request, WebResponseWriter& response) {
    if (!authenticateAdmin(request, response)) return;
    Serial.println("[WEB] Restart requested");
    sendText(response, 202U, "Restart zaplanowany. Urządzenie uruchomi się ponownie.");
    scheduleRestart();
}

void WebManager::handleUpdatePage(const WebRequest& request, WebResponseWriter& response) {
    if (!authenticateAdmin(request, response)) return;
    response.beginResponse(200U, ContentType::Html);
    response.write(UPDATE_PAGE, strlen_P(UPDATE_PAGE));
    response.endResponse();
}

void WebManager::handleUpdateResult(const WebRequest& request, WebResponseWriter& response) {
    if (!authenticateAdmin(request, response)) return;
    if (otaSuccessful)
        sendText(response, 200U, "Aktualizacja zakończona. Urządzenie uruchomi się ponownie.");
    else
    {
        response.beginResponse(500U, ContentType::PlainText);
        response.writeText("Aktualizacja nieudana: ");
        response.writeText(otaError.c_str());
        response.endResponse();
    }
}

void WebManager::handleUpload(const WebRequest& request, const WebUploadEvent& event) {
    if (event.status == WebUploadStatus::Start) {
        const bool authorized = adminPassword_ != nullptr &&
                                strcmp(adminPassword_, "CHANGE_ME_BEFORE_USE") != 0 &&
                                adminPassword_[0] != '\0' &&
                                request.authenticateBasic(adminUser_, adminPassword_);
        if (!authorized) return;
        resetUploadState();
        otaAuthorized = true;
        char sizeHeader[32] {};
        request.copyHeader("X-Firmware-Size", sizeHeader, sizeof(sizeHeader));
        expectedFirmwareSize = static_cast<size_t>(String(sizeHeader).toInt());
        String filename = event.filename != nullptr ? event.filename : "";
        filename.toLowerCase();
        const size_t available = runtime_->availableFirmwareSpace();
        if (!filename.endsWith(".bin")) { failOta("Dozwolony jest wyłącznie plik .bin"); return; }
        if (expectedFirmwareSize == 0) { failOta("Brak poprawnego rozmiaru pliku"); return; }
        if (expectedFirmwareSize > available) { failOta("Plik jest większy niż partycja OTA"); return; }
        runtime_->stopPumps();
        runtime_->setOtaInProgress(true);
        otaInProgress = true;
        if (!runtime_->beginFirmwareUpdate(expectedFirmwareSize)) {
            failOta(runtime_->firmwareError()); return;
        }
        otaAccepted = true;
        Serial.printf("[WEB] OTA upload started: %u bytes, available: %u bytes\n",
                      static_cast<unsigned>(expectedFirmwareSize), static_cast<unsigned>(available));
    } else if (event.status == WebUploadStatus::Chunk) {
        if (!otaAuthorized || !otaAccepted) return;
        serviceDuringUpload();
        if (runtime_->writeFirmware(event.data, event.dataLength) != event.dataLength) {
            failOta(runtime_->firmwareError());
            return;
        }
        const uint8_t percent = static_cast<uint8_t>((event.bytesReceived * 100ULL) / expectedFirmwareSize);
        while (nextProgressPercent <= 100 && percent >= nextProgressPercent) {
            Serial.printf("[WEB] OTA progress: %u%%\n", nextProgressPercent);
            nextProgressPercent += 25;
        }
    } else if (event.status == WebUploadStatus::End) {
        if (!otaAuthorized || !otaAccepted) return;
        if (event.bytesReceived != expectedFirmwareSize) { failOta("Rozmiar odebranego pliku jest niezgodny"); return; }
        if (!runtime_->endFirmwareUpdate()) { failOta(runtime_->firmwareError()); return; }
        otaSuccessful = true;
        otaAuthorized = false;
        otaAccepted = false;
        expectedFirmwareSize = 0U;
        Serial.println("[WEB] OTA successful");
        scheduleRestart();
    } else if (event.status == WebUploadStatus::Abort && otaAuthorized) {
        failOta("Wysyłanie przerwane");
    }
}

void WebManager::failOta(const String& message) {
    if (otaAccepted) runtime_->abortFirmwareUpdate();
    otaAccepted = false;
    otaSuccessful = false;
    otaInProgress = false;
    otaAuthorized = false;
    expectedFirmwareSize = 0U;
    otaError = message;
    if (runtime_ != nullptr) runtime_->setOtaInProgress(false);
    Serial.printf("[WEB] OTA failed: %s\n", message.c_str());
}

void WebManager::resetUploadState() {
    if (otaAccepted) runtime_->abortFirmwareUpdate();
    if (otaInProgress && runtime_ != nullptr) runtime_->setOtaInProgress(false);
    otaInProgress = false;
    otaAuthorized = false;
    otaAccepted = false;
    otaSuccessful = false;
    otaError = "Nieznany błąd";
    expectedFirmwareSize = 0U;
    nextProgressPercent = 25U;
}

void WebManager::serviceDuringUpload() {
    runtime_->serviceDuringUpload();
}
void WebManager::scheduleRestart() {
    restartPending = true;
    restartAt = runtime_->nowMs() + RESTART_DELAY_MS;
}

bool WebManager::isOtaInProgress() const { return otaInProgress; }
bool WebManager::isRestartPending() const { return restartPending; }

void WebManager::restartRoute(void* context, const WebRequest& request,
                              WebResponseWriter& response) {
    static_cast<WebManager*>(context)->handleRestart(request, response);
}

void WebManager::updatePageRoute(void* context, const WebRequest& request,
                                 WebResponseWriter& response) {
    static_cast<WebManager*>(context)->handleUpdatePage(request, response);
}

void WebManager::updateResultRoute(void* context, const WebRequest& request,
                                   WebResponseWriter& response) {
    static_cast<WebManager*>(context)->handleUpdateResult(request, response);
}

void WebManager::uploadRoute(void* context, const WebRequest& request,
                             const WebUploadEvent& event) {
    static_cast<WebManager*>(context)->handleUpload(request, event);
}