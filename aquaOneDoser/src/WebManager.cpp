#include "WebManager.h"

#include "DiagnosticsManager.h"
#include "MqttManager.h"
#include "PumpDriver.h"
#include "SchedulerManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "app_config.h"
#include "secrets.h"

#include <Update.h>
#include <WiFi.h>
#include <time.h>
#include <cstring>

namespace {
const char MAIN_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PMW AquaDoser</title><style>
body{font-family:system-ui,sans-serif;background:#eef4f7;color:#17313d;margin:0;padding:18px}.wrap{max-width:820px;margin:auto}h1{margin:.2em 0}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}.card{background:white;border-radius:12px;padding:16px;box-shadow:0 2px 10px #0002}.row{display:flex;justify-content:space-between;gap:14px;padding:5px 0;border-bottom:1px solid #e5ecef}.row:last-child{border:0}.v{text-align:right;font-weight:600}.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}button,a.btn{border:0;border-radius:8px;padding:11px 15px;background:#126b86;color:white;font-weight:700;text-decoration:none;cursor:pointer}.danger{background:#a52a2a}#msg{margin-top:10px}@media(max-width:500px){body{padding:10px}.row{font-size:.93rem}}
</style></head><body><div class="wrap"><h1>PMW AquaDoser</h1><p>Serwisowy panel statusu</p><div class="grid">
<section class="card"><h2>Status</h2><div class="row"><span>Firmware</span><span class="v" id="firmware">-</span></div><div class="row"><span>Uptime</span><span class="v" id="uptime">-</span></div><div class="row"><span>Status systemu</span><span class="v" id="system_status">-</span></div></section>
<section class="card"><h2>Sieć</h2><div class="row"><span>Wi-Fi</span><span class="v" id="wifi">-</span></div><div class="row"><span>SSID</span><span class="v" id="ssid">-</span></div><div class="row"><span>IP</span><span class="v" id="ip">-</span></div><div class="row"><span>RSSI</span><span class="v" id="rssi">-</span></div></section>
<section class="card"><h2>MQTT</h2><div class="row"><span>MQTT</span><span class="v" id="mqtt">-</span></div><div class="row"><span>Broker</span><span class="v" id="broker">-</span></div></section>
<section class="card"><h2>Czas</h2><div class="row"><span>RTC</span><span class="v" id="rtc">-</span></div><div class="row"><span>NTP</span><span class="v" id="ntp">-</span></div><div class="row"><span>Czas lokalny</span><span class="v" id="local_time">-</span></div><div class="row"><span>Ostatnie NTP</span><span class="v" id="last_ntp">-</span></div></section>
<section class="card"><h2>System</h2><div class="row"><span>Free Heap</span><span class="v" id="free_heap">-</span></div><div class="row"><span>Firmware</span><span class="v" id="firmware2">-</span></div><div class="row"><span>Uptime</span><span class="v" id="uptime2">-</span></div></section></div>
<div class="actions"><a class="btn" href="/update">Aktualizacja firmware</a><form method="post" action="/api/restart" onsubmit="return confirm('Czy na pewno zrestartować PMW AquaDoser?')"><button class="danger" type="submit">RESTART URZĄDZENIA</button></form></div><div id="msg"></div></div>
<script>function yn(v,a,b){return v?a:b}async function refresh(){try{let r=await fetch('/api/status',{cache:'no-store'});let d=await r.json();for(let k of ['firmware','system_status','ssid','ip','broker','local_time','last_ntp'])document.getElementById(k).textContent=d[k];document.getElementById('firmware2').textContent=d.firmware;document.getElementById('uptime').textContent=d.uptime+' s';document.getElementById('uptime2').textContent=d.uptime+' s';document.getElementById('wifi').textContent=yn(d.wifi,'Połączono','Rozłączono');document.getElementById('mqtt').textContent=yn(d.mqtt,'Połączono','Rozłączono');document.getElementById('rtc').textContent=yn(d.rtc,'OK','BŁĄD');document.getElementById('ntp').textContent=yn(d.ntp,'OK','BRAK');document.getElementById('rssi').textContent=d.rssi+' dBm';document.getElementById('free_heap').textContent=d.free_heap+' B'}catch(e){document.getElementById('msg').textContent='Brak połączenia z urządzeniem'}}refresh();setInterval(refresh,3000);</script></body></html>
)HTML";

const char UPDATE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Aktualizacja PMW AquaDoser</title><style>body{font-family:system-ui;background:#eef4f7;padding:20px}.box{max-width:560px;margin:auto;background:#fff;padding:22px;border-radius:12px}button{padding:11px 16px;background:#126b86;color:#fff;border:0;border-radius:8px;font-weight:700}progress{width:100%;height:22px;margin-top:14px}</style></head><body><div class="box"><h1>PMW AquaDoser — Aktualizacja firmware</h1><p>Wybierz plik <code>firmware.bin</code> wygenerowany przez PlatformIO.</p><form id="f"><input id="file" type="file" accept=".bin,application/octet-stream" required><button>AKTUALIZUJ</button></form><progress id="p" value="0" max="100"></progress><p id="m"></p><p><a href="/">Powrót</a></p></div><script>document.getElementById('f').onsubmit=function(e){e.preventDefault();let file=document.getElementById('file').files[0];if(!file||!file.name.toLowerCase().endsWith('.bin')){m.textContent='Wybierz plik .bin';return}if(!confirm('Rozpocząć aktualizację firmware?'))return;let x=new XMLHttpRequest(),fd=new FormData();fd.append('firmware',file);x.open('POST','/update');x.setRequestHeader('X-Firmware-Size',file.size);x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded*100/e.total};x.onload=()=>{m.textContent=x.responseText};x.onerror=()=>{m.textContent='Błąd połączenia podczas aktualizacji'};m.textContent='Wysyłanie...';x.send(fd)};</script></body></html>
)HTML";
}

bool WebManager::begin(TimeManager& timeManager, MqttManager& mqttManager,
                       DiagnosticsManager& diagnosticsManager, SchedulerManager& schedulerManager,
                       WiFiManager& wifiManager, PumpDriver& pumpDriver) {
    time = &timeManager;
    mqtt = &mqttManager;
    diagnostics = &diagnosticsManager;
    scheduler = &schedulerManager;
    wifi = &wifiManager;
    driver = &pumpDriver;
    configureRoutes();
    return true;
}

void WebManager::configureRoutes() {
    if (routesConfigured) return;
    const char* headers[] = {"X-Firmware-Size"};
    server.collectHeaders(headers, 1);
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server.on("/api/restart", HTTP_POST, [this]() { handleRestart(); });
    server.on("/update", HTTP_GET, [this]() { handleUpdatePage(); });
    server.on("/update", HTTP_POST, [this]() { handleUpdateResult(); }, [this]() { handleUpload(); });
    server.onNotFound([this]() { server.send(404, "text/plain; charset=utf-8", "Not found"); });
    routesConfigured = true;
}

void WebManager::loop() {
    const bool connected = WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
    if (connected && !wifiWasConnected) startServer();
    if (!connected && wifiWasConnected) {
        server.stop();
        serverStarted = false;
        wifiWasConnected = false;
        Serial.println("[WEB] Server stopped - Wi-Fi unavailable");
    }
    if (connected && serverStarted) server.handleClient();
    if (restartPending && static_cast<long>(millis() - restartAt) >= 0) {
        Serial.println("[WEB] Restarting device");
        if (driver != nullptr) driver->stopAll();
        ESP.restart();
    }
}

void WebManager::startServer() {
    if (serverStarted) server.stop();
    server.begin();
    serverStarted = true;
    wifiWasConnected = true;
    Serial.println("[WEB] Server started");
    Serial.printf("[WEB] URL: http://%s/\n", WiFi.localIP().toString().c_str());
}

bool WebManager::authenticateAdmin() {
    if (strcmp(WEB_PASS, "CHANGE_ME_BEFORE_USE") == 0 || WEB_PASS[0] == '\0') {
        server.send(503, "text/plain; charset=utf-8",
                    "Funkcje administracyjne są wyłączone. Ustaw WEB_PASS w secrets.h.");
        return false;
    }
    if (server.authenticate(WEB_USER, WEB_PASS)) return true;
    server.requestAuthentication(BASIC_AUTH, "PMW AquaDoser");
    return false;
}
void WebManager::handleRoot() {
    server.send_P(200, "text/html; charset=utf-8", MAIN_PAGE);
}

String WebManager::jsonEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c == '"' || c == '\\') { escaped += '\\'; escaped += c; }
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') escaped += "\\r";
        else if (static_cast<uint8_t>(c) >= 0x20) escaped += c;
    }
    return escaped;
}

String WebManager::formatLocalTimestamp(uint32_t timestamp) {
    if (timestamp < 1700000000UL) return "BRAK";
    const time_t raw = static_cast<time_t>(timestamp);
    tm local{};
    localtime_r(&raw, &local);
    char text[24];
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
    return String(text);
}

void WebManager::handleStatus() {
    const tm local = time->getLocalTime();
    char localText[24] = "BRAK";
    if (time->isRtcOk() && local.tm_year + 1900 >= 2023)
        strftime(localText, sizeof(localText), "%Y-%m-%d %H:%M:%S", &local);
    String json;
    json.reserve(420);
    json = "{\"firmware\":\"" APP_VERSION "\",\"uptime\":" + String(millis() / 1000UL);
    json += ",\"system_status\":\"" + jsonEscape(diagnostics->getSystemStatusText()) + "\"";
    json += ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    json += ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"";
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    json += ",\"mqtt\":" + String(mqtt->isConnected() ? "true" : "false");
    json += ",\"broker\":\"" + jsonEscape(String(MQTT_HOST)) + "\"";
    json += ",\"rtc\":" + String(time->isRtcOk() ? "true" : "false");
    json += ",\"ntp\":" + String(time->isNtpSynced() ? "true" : "false");
    json += ",\"local_time\":\"" + jsonEscape(String(localText)) + "\"";
    json += ",\"last_ntp\":\"" + jsonEscape(formatLocalTimestamp(time->getLastNtpSyncTimestamp())) + "\"";
    json += ",\"free_heap\":" + String(ESP.getFreeHeap()) + "}";
    server.send(200, "application/json; charset=utf-8", json);
}

void WebManager::handleRestart() {
    if (!authenticateAdmin()) return;
    Serial.println("[WEB] Restart requested");
    server.send(202, "text/plain; charset=utf-8", "Restart zaplanowany. Urządzenie uruchomi się ponownie.");
    scheduleRestart();
}

void WebManager::handleUpdatePage() {
    if (!authenticateAdmin()) return;
    server.send_P(200, "text/html; charset=utf-8", UPDATE_PAGE);
}

void WebManager::handleUpdateResult() {
    if (!authenticateAdmin()) return;
    if (otaSuccessful)
        server.send(200, "text/plain; charset=utf-8", "Aktualizacja zakończona. Urządzenie uruchomi się ponownie.");
    else
        server.send(500, "text/plain; charset=utf-8", "Aktualizacja nieudana: " + otaError);
}

void WebManager::handleUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        otaAuthorized = strcmp(WEB_PASS, "CHANGE_ME_BEFORE_USE") != 0 &&
                        WEB_PASS[0] != '\0' && server.authenticate(WEB_USER, WEB_PASS);
        otaAccepted = false;
        otaSuccessful = false;
        otaError = "Nieznany błąd";
        nextProgressPercent = 25;
        if (!otaAuthorized) return;
        expectedFirmwareSize = static_cast<size_t>(server.header("X-Firmware-Size").toInt());
        String filename = upload.filename;
        filename.toLowerCase();
        const size_t available = ESP.getFreeSketchSpace();
        if (!filename.endsWith(".bin")) { failOta("Dozwolony jest wyłącznie plik .bin"); return; }
        if (expectedFirmwareSize == 0) { failOta("Brak poprawnego rozmiaru pliku"); return; }
        if (expectedFirmwareSize > available) { failOta("Plik jest większy niż partycja OTA"); return; }
        if (driver != nullptr) driver->stopAll();
        scheduler->setOtaInProgress(true);
        otaInProgress = true;
        if (!Update.begin(expectedFirmwareSize, U_FLASH)) { failOta(String(Update.errorString())); return; }
        otaAccepted = true;
        Serial.printf("[WEB] OTA upload started: %u bytes, available: %u bytes\n",
                      static_cast<unsigned>(expectedFirmwareSize), static_cast<unsigned>(available));
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!otaAuthorized || !otaAccepted) return;
        serviceDuringUpload();
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            failOta(String(Update.errorString()));
            return;
        }
        const size_t uploaded = upload.totalSize + upload.currentSize;
        const uint8_t percent = static_cast<uint8_t>((uploaded * 100ULL) / expectedFirmwareSize);
        while (nextProgressPercent <= 100 && percent >= nextProgressPercent) {
            Serial.printf("[WEB] OTA progress: %u%%\n", nextProgressPercent);
            nextProgressPercent += 25;
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!otaAuthorized || !otaAccepted) return;
        if (upload.totalSize != expectedFirmwareSize) { failOta("Rozmiar odebranego pliku jest niezgodny"); return; }
        if (!Update.end(true)) { failOta(String(Update.errorString())); return; }
        otaSuccessful = true;
        Serial.println("[WEB] OTA successful");
        scheduleRestart();
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (otaAccepted) Update.abort();
        failOta("Wysyłanie przerwane");
    }
}

void WebManager::failOta(const String& message) {
    if (otaAccepted) Update.abort();
    otaAccepted = false;
    otaSuccessful = false;
    otaInProgress = false;
    otaError = message;
    if (scheduler != nullptr) scheduler->setOtaInProgress(false);
    Serial.printf("[WEB] OTA failed: %s\n", message.c_str());
}

void WebManager::serviceDuringUpload() {
    if (driver != nullptr) driver->loop();
    if (wifi != nullptr) wifi->loop();
    if (time != nullptr) time->loop();
    if (scheduler != nullptr) scheduler->loop();
    if (diagnostics != nullptr) diagnostics->loop();
    if (mqtt != nullptr) mqtt->loop();
    yield();
}
void WebManager::scheduleRestart() {
    restartPending = true;
    restartAt = millis() + RESTART_DELAY_MS;
}

bool WebManager::isOtaInProgress() const { return otaInProgress; }