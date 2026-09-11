#include "ConfigPortal.h"
#include "ConfigAccessPoint.h"
#include "ConfigStore.h"
#include "ConfigValidator.h"
#include "ConfigDefaults.h"
#include "FirmwareVersion.h"
#include "SystemSettings.h"
#include "SerialDiagnostics.h"
#include "BacklightController.h"
#include <WiFi.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "DebugLog.h"
namespace {
constexpr char TYPE[] = "text/html; charset=utf-8";
const char PAGE[] PROGMEM =
    R"(<!doctype html><html lang="pl"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>yoPILOT &mdash; Konfiguracja</title><style>body{margin:0;background:#101827;color:#e5e7eb;font:16px system-ui,sans-serif}.page{max-width:720px;margin:auto;padding:20px}h1{margin:0;color:#fff;font-size:28px}h1 span{color:#65d46e}.radio-state.on{color:#65d46e}.radio-state.off{color:#9ca3af}.footer{margin:28px 0 8px;text-align:center;color:#9ca3af;font-size:13px}.footer .yo{color:#fff}.footer .pilot{color:#65d46e}h2{font-size:18px;margin:0 0 12px;color:#fff}.sub,.hint{color:#9ca3af}.sub{margin:4px 0 20px}.hint{margin:5px 0 0;font-size:13px}.card{background:#1f2937;border:1px solid #374151;border-radius:12px;padding:16px;margin:12px 0}.row{display:grid;grid-template-columns:minmax(130px,40%) 1fr;gap:10px;padding:7px 0;border-top:1px solid #374151}.row:first-of-type{border-top:0}.key{color:#9ca3af}.value{overflow-wrap:anywhere;color:#fff}.value input{box-sizing:border-box;width:100%;background:#111827;color:#fff;border:1px solid #4b5563;border-radius:7px;padding:8px;font:inherit}.value input:disabled{opacity:.45}.radio{margin-top:12px;padding-top:12px;border-top:1px solid #374151}.radio summary{cursor:pointer;color:#fff;padding:5px 0;min-height:24px}.actions{margin:16px 0}.actions p{margin:10px 0}.unit{margin-left:6px}.tag{display:inline-block;border-radius:999px;padding:2px 8px;background:#14532d;color:#bbf7d0;font-size:13px}.notice{border-radius:9px;padding:10px 12px;margin:12px 0}.ok{background:#14532d;color:#bbf7d0}.err{background:#7f1d1d;color:#fecaca}.submit{width:100%;padding:11px;border:0;border-radius:8px;background:#65d46e;color:#102018;font-weight:700;font:inherit}.check{background:#374151;color:#fff;border:1px solid #4b5563}@media(max-width:460px){.page{padding:14px}.row{grid-template-columns:1fr;gap:2px}}</style></head><body><main class="page"><h1>yo<span>PILOT</span></h1><p class="sub">Konfiguracja</p><form method="post" action="/validate">)";
const char END[] PROGMEM =
    "</form><footer class=\"footer\"><span class=\"yo\">yo</span><span class=\"pilot\">PILOT</span> &bull; Project by pimowo &bull; 2026</footer></main><script>const d=document.querySelector('[name=dhcp]');"
    "function staticFields(){if(!d)return;document.querySelectorAll('.static-section').forEach(e=>e.hidden=d.checked);document.querySelectorAll('.static-ip').forEach(e=>e.disabled=d.checked)}"
    "function collapseRadios(){document.querySelectorAll('details.radio-config').forEach(e=>e.open=false)}"
    "if(d){d.addEventListener('change',staticFields);staticFields()}document.addEventListener('DOMContentLoaded',collapseRadios);addEventListener('pageshow',collapseRadios);</script></body></html>";
void rn(char *out, size_t n, uint8_t i, const char *part) {
  snprintf(out, n, "r%u_%s", i, part);
}
} // namespace

ConfigPortal::ConfigPortal(const ConfigAccessPoint &ap, ConfigStore &store, SystemSettings& settings, SystemSettingsStore& settingsStore, BacklightController& backlight)
    : accessPoint_(ap), configStore_(store), systemSettings_(settings), systemSettingsStore_(settingsStore), backlight_(backlight) {}
void ConfigPortal::begin(const RuntimeConfig &config) {
  sourceConfig_ = &config;
  workingConfig_ = config;
  workingSystemSettings_ = systemSettings_;
  server_.on("/", HTTP_GET, [this] { handleRoot(); });
  server_.on("/validate", HTTP_POST, [this] { handleValidate(); });
  server_.on("/save", HTTP_POST, [this] { handleSave(); });
  DEBUG_LOGF("[CONFIG_WEB][%lu] starting\n", millis());
  server_.begin();
  running_ = true;
  DEBUG_LOGF("[CONFIG_WEB][%lu] READY http://192.168.4.1/\n", millis());
}
void ConfigPortal::loop() {
  if (running_)
    server_.handleClient();
  if (restartPending_ && millis() - restartScheduledAt_ >= 1000) {
    DEBUG_LOGF("[CONFIG_WEB][%lu] restarting\n", millis());
    ESP.restart();
  }
}
void ConfigPortal::handleRoot() {
  if (!sourceConfig_) {
    server_.send(503, "text/plain; charset=utf-8",
                 "Konfiguracja jest niedostÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬ÄąÄľĂ„â€šĂ‹ÂÄ‚ËĂ˘â‚¬ĹˇĂ‚Â¬Ă„Ä…Ă‹â€ˇÄ‚â€žĂ˘â‚¬ĹˇÄ‚â€ąĂ‚ÂĂ„â€šĂ‹ÂÄ‚ËĂ˘â€šÂ¬ÄąË‡Ä‚â€šĂ‚Â¬Ä‚â€žĂ„â€¦Ä‚â€žĂ„ÄľĂ„â€šĂ˘â‚¬ĹľÄ‚ËĂ˘â€šÂ¬ÄąË‡Ă„â€šĂ˘â‚¬Ä…Ä‚â€šĂ‚ÂÄ‚â€žĂ˘â‚¬ĹˇÄ‚â€ąĂ‚ÂĂ„â€šĂ‹ÂÄ‚ËĂ˘â€šÂ¬ÄąË‡Ä‚â€šĂ‚Â¬Ä‚â€žĂ„â€¦Ä‚â€žĂ„ÄľÄ‚â€žĂ˘â‚¬ĹˇÄ‚ËĂ˘â€šÂ¬Ă„â€¦Ă„â€šĂ˘â‚¬ĹˇÄ‚â€šĂ‚Âpna.");
    return;
  }
  statusMessage_ = nullptr;
  DEBUG_LOGF("[CONFIG_WEB][%lu] GET /\n", millis());
  renderPage();
}
void ConfigPortal::handleValidate() {
  DEBUG_LOGF("[CONFIG_WEB][%lu] POST /validate\n", millis());
  RuntimeConfig& c = candidateConfig_;
  c = workingConfig_;
  ConfigValidationError e = ConfigValidationError::NONE;
  uint8_t r = 0;
  if (!buildCandidate(c, e, r)) {
    statusSuccess_ = false;
    statusMessage_ = errorMessage(e, r);
    DEBUG_LOGF("[CONFIG_WEB][%lu] validation FAILED: %u\n", millis(),
                  unsigned(e));
    renderPage();
    return;
  }
  SystemSettings candidateSettings = workingSystemSettings_;
  if (!buildSystemSettings(candidateSettings)) {
    statusSuccess_ = false;
    statusMessage_ = "Jasno&#347;&#263; LCD musi by&#263; w zakresie 0&ndash;100%.";
    renderPage();
    return;
  }  ValidationResult result;
  if (!ConfigValidator::validate(c, result)) {
    statusSuccess_ = false;
    statusMessage_ = formatValidationError(result);
    DEBUG_LOGF("[CONFIG_WEB][%lu] validation FAILED: %u\n", millis(),
                  unsigned(result.error));
    renderPage();
    return;
  }
  workingConfig_ = c;
  workingSystemSettings_ = candidateSettings;
  statusSuccess_ = true;
  statusMessage_ = "Konfiguracja poprawna. Zmiany s&#261; aktywne tylko do "
                   "resetu urz&#261;dzenia.";
  DEBUG_LOGF("[CONFIG_WEB][%lu] validation OK\n", millis());
  renderPage();
}
void ConfigPortal::handleSave() {
  DEBUG_LOGF("[CONFIG_WEB][%lu] POST /save\n", millis());
  if (restartPending_) {
    statusSuccess_ = true;
    statusMessage_ = "Restart zosta&#322; ju&#380; zaplanowany.";
    renderPage();
    return;
  }
  RuntimeConfig& c = candidateConfig_;
  c = workingConfig_;
  ConfigValidationError e = ConfigValidationError::NONE;
  uint8_t r = 0;
  if (!buildCandidate(c, e, r)) {
    statusSuccess_ = false;
    statusMessage_ = errorMessage(e, r);
    DEBUG_LOGF("[CONFIG_WEB][%lu] save validation FAILED: %u\n", millis(),
                  unsigned(e));
    renderPage();
    return;
  }
  ValidationResult result;
  if (!ConfigValidator::validate(c, result)) {
    statusSuccess_ = false;
    statusMessage_ = formatValidationError(result);
    DEBUG_LOGF("[CONFIG_WEB][%lu] save validation FAILED: %u\n", millis(),
                  unsigned(result.error));
    renderPage();
    return;
  }
  DEBUG_LOGF("[CONFIG_WEB][%lu] save validation OK\n", millis());
  SystemSettings updatedSettings = workingSystemSettings_;
  if (!buildSystemSettings(updatedSettings)) {
    statusSuccess_ = false;
    statusMessage_ = "Jasno&#347;&#263; LCD musi by&#263; w zakresie 0&ndash;100%.";
    renderPage();
    return;
  }

  SystemSettingsSnapshot previousSystemSettings{};
  if (!configStore_.capturePrimaryForRollback() ||
      !systemSettingsStore_.captureSnapshot(previousSystemSettings)) {
    configStore_.clearRollbackSnapshot();
    statusSuccess_ = false;
    statusMessage_ = "Nie uda&#322;o si&#281; przygotowa&#263; bezpiecznego zapisu konfiguracji.";
    DEBUG_LOGLN("[CONFIG_WEB] save failed");
    renderPage();
    return;
  }

  const bool configSaved = configStore_.save(c);
  bool systemWriteStarted = false;
  bool saveSucceeded = configSaved;
  if (saveSucceeded) {
    systemWriteStarted = true;
    saveSucceeded = systemSettingsStore_.saveBrightness(updatedSettings.lcdBrightnessPercent) &&
                    systemSettingsStore_.saveSerialDebug(updatedSettings.serialDebug) &&
                    systemSettingsStore_.saveUsbSleepInhibit(updatedSettings.usbSleepInhibit);
  }

  if (!saveSucceeded) {
    const bool configRestored = configStore_.restorePrimaryFromRollback();
    const bool settingsRestored = !systemWriteStarted ||
                                  systemSettingsStore_.restoreSnapshot(previousSystemSettings);
    configStore_.clearRollbackSnapshot();
    statusSuccess_ = false;
    if (configRestored && settingsRestored) {
      statusMessage_ = "Zapis konfiguracji nie powi&oacute;d&#322; si&#281;. Przywr&oacute;cono poprzednie ustawienia.";
    } else {
      statusMessage_ = "KRYTYCZNY B&#321;&#260;D: zapis i rollback nie powiod&#322;y si&#281;. Wejd&#378; do Config Mode i zapisz konfiguracj&#281; ponownie.";
      DEBUG_LOGLN("[CONFIG_WEB] rollback failed");
      Serial.printf("[CONFIG_WEB][%lu] rollback FAILED\n", millis());
    }
    DEBUG_LOGLN("[CONFIG_WEB] save failed");
    Serial.printf("[CONFIG_WEB][%lu] save FAILED\n", millis());
    renderPage();
    return;
  }
  configStore_.clearRollbackSnapshot();
  systemSettings_ = updatedSettings;
  workingSystemSettings_ = updatedSettings;
  backlight_.setBrightnessPercent(updatedSettings.lcdBrightnessPercent);
  SerialDiagnostics::setEnabled(updatedSettings.serialDebug);
  workingConfig_ = c;
  statusSuccess_ = true;
  statusMessage_ = "Konfiguracja zosta&#322;a zapisana. Pilot uruchomi "
                   "si&#281; ponownie. Po restarcie po&#322;&#261;cz si&#281; "
                   "ponownie z pilotem, je&#347;li b&#281;dzie to potrzebne.";
  DEBUG_LOGF("[CONFIG_WEB][%lu] config saved\n", millis());
  renderPage();
  restartScheduledAt_ = millis();
  restartPending_ = true;
  DEBUG_LOGF("[CONFIG_WEB][%lu] restart scheduled\n", millis());
}
void ConfigPortal::renderPage() {
  constexpr size_t kPageReserveBytes = 32768;
  const uint32_t heapBefore = ESP.getFreeHeap();
  size_t pageBytes = 0;
  uint32_t heapDuring = heapBefore;
  {
    String page;
    page.reserve(kPageReserveBytes);
    renderBuffer_ = &page;
    sendPageStart();
    sendStatus();
    sendNetworkSection();
    sendRadioSection();
    sendTimeSection();
    sendRemoteSection();
    sendSystemSection();
    sendStatic(END);
    renderBuffer_ = nullptr;
    pageBytes = page.length();
    heapDuring = ESP.getFreeHeap();

    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, TYPE, page);
  }
  DEBUG_LOGF("[CONFIG_WEB][%lu] GET / bytes=%u heap=%lu/%lu/%lu min=%lu\n",
             millis(), static_cast<unsigned>(pageBytes),
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned long>(heapDuring),
             static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(ESP.getMinFreeHeap()));
}
void ConfigPortal::sendPageStart() { sendStatic(PAGE); }
void ConfigPortal::sendStatus() {
  if (!statusMessage_)
    return;
  sendStatic(statusSuccess_ ? "<div class=\"notice ok\">"
                            : "<div class=\"notice err\">");
  sendStatic(statusMessage_);
  sendStatic("</div>");
}
void ConfigPortal::sendNetworkSection() {
  sendStatic("<section class=\"card\"><h2>Sie&#263; Wi-Fi</h2>");
  const char* labels[] = {"SSID", "Has&#322;o Wi-Fi", "DHCP", "Statyczny adres IP", "Brama", "Maska podsieci", "DNS"};
  const char* names[] = {"ssid", "password", "dhcp", "static_ip", "gateway", "subnet", "dns"};
  const char* values[] = {workingConfig_.wifiSsid, "", "", workingConfig_.staticIp, workingConfig_.gatewayIp, workingConfig_.subnetMask, workingConfig_.dns1Ip};
  const char* hints[] = {"Nazwa sieci Wi-Fi, z kt&oacute;r&#261; po&#322;&#261;czy si&#281; pilot.", "Pozostaw puste, aby zachowa&#263; obecne has&#322;o.<br>W nowej konfiguracji puste oznacza sie&#263; otwart&#261;.", "W&#322;&#261;czone: adresy poni&#380;ej s&#261; pobierane automatycznie z routera.", "Adres IP pilota w sieci lokalnej.", "Adres routera w sieci lokalnej.", "Maska sieci lokalnej.", "Serwer DNS u&#380;ywany przy statycznej konfiguracji sieci."};
  const size_t limits[] = {RUNTIME_WIFI_SSID_LEN - 1, RUNTIME_WIFI_PASS_LEN - 1, 0, RUNTIME_IP_LEN - 1, RUNTIME_IP_LEN - 1, RUNTIME_IP_LEN - 1, RUNTIME_IP_LEN - 1};
  for (uint8_t i = 0; i < 7; ++i) {
    sendStatic(i >= 3 ? "<div class=\"row static-section\">" : "<div class=\"row\">");
    sendStatic("<label class=\"key\" for=\""); sendStatic(names[i]); sendStatic("\">"); sendStatic(labels[i]);
    sendStatic("</label><span class=\"value\">");
    if (i == 2) sendCheckbox(names[i], !workingConfig_.useStaticIp);
    else sendInput(names[i], values[i], i == 1 ? "password" : "text", i >= 3 && !workingConfig_.useStaticIp, limits[i]);
    sendStatic("<p class=\"hint\">"); sendStatic(hints[i]); sendStatic("</p></span></div>");
  }
  sendStatic("</section>");
}

void ConfigPortal::sendRadioSection() {
  sendStatic("<section class=\"card\"><h2>Radia</h2>");
  for (uint8_t i = 0; i < YORADIO_MAX; ++i) {
    auto& radio = workingConfig_.radios[i];
    char enabled[16], name[16], host[16], port[16], path[16];
    rn(enabled, sizeof enabled, i, "enabled"); rn(name, sizeof name, i, "name"); rn(host, sizeof host, i, "host"); rn(port, sizeof port, i, "port"); rn(path, sizeof path, i, "path");
    sendStatic("<details class=\"radio radio-config\"><summary>Radio ");
    sendUnsigned(i + 1);
    if (radio.name[0] != '\0') { sendStatic(" &mdash; "); sendEscaped(radio.name); }
    sendStatic(radio.enabled ? " <span class=\"radio-state on\">&mdash; w&#322;&#261;czone</span>" : " <span class=\"radio-state off\">&mdash; wy&#322;&#261;czone</span>");
    sendStatic("</summary><div class=\"row\"><label class=\"key\" for=\""); sendStatic(enabled); sendStatic("\">W&#322;&#261;czone</label><span class=\"value\">");
    sendCheckbox(enabled, radio.enabled);
    sendStatic("<p class=\"hint\">Zaznacz radio, aby by&#322;o dost&#281;pne na li&#347;cie wyboru pilota.</p></span></div>");
    const char* labels[] = {"Nazwa radia", "Adres radia"};
    const char* fieldNames[] = {name, host};
    const char* values[] = {radio.name, radio.host};
    const char* hints[] = {"Nazwa widoczna na li&#347;cie wyboru w pilocie.", "Adres IP lub nazwa hosta yoRadio."};
    const size_t limits[] = {RUNTIME_RADIO_NAME_LEN - 1, RUNTIME_RADIO_HOST_LEN - 1};
    for (uint8_t field = 0; field < 2; ++field) {
      sendStatic("<div class=\"row\"><label class=\"key\" for=\""); sendStatic(fieldNames[field]); sendStatic("\">"); sendStatic(labels[field]);
      sendStatic("</label><span class=\"value\">"); sendInput(fieldNames[field], values[field], "text", false, limits[field]);
      sendStatic("<p class=\"hint\">"); sendStatic(hints[field]); sendStatic("</p></span></div>");
    }
    char portText[6]{}; snprintf(portText, sizeof portText, "%u", radio.wsPort);
    sendStatic("<details><summary>Ustawienia zaawansowane</summary><div class=\"row\"><label class=\"key\" for=\""); sendStatic(port); sendStatic("\">Port</label><span class=\"value\">");
    sendNumberInput(port, portText, 0, 65535);
    sendStatic("<p class=\"hint\">Port serwera WWW/WebSocket yoRadio. Domy&#347;lnie: "); sendUnsigned(ConfigDefaults::kWebSocketPort);
    sendStatic(".</p></span></div><div class=\"row\"><label class=\"key\" for=\""); sendStatic(path); sendStatic("\">&#346;cie&#380;ka WebSocket</label><span class=\"value\">");
    sendInput(path, radio.wsPath, "text", false, RUNTIME_RADIO_PATH_LEN - 1);
    sendStatic("<p class=\"hint\">&#346;cie&#380;ka po&#322;&#261;czenia WebSocket. Domy&#347;lnie: "); sendStatic(ConfigDefaults::kWebSocketPath); sendStatic(".</p></span></div></details></details>");
  }
  sendStatic("</section>");
}

void ConfigPortal::sendTimeSection() {
  sendStatic("<section class=\"card\"><h2>Czas</h2>");
  sendStatic("<div class=\"row\"><label class=\"key\" for=\"timezone\">Strefa czasowa</label><span class=\"value\">");
  sendInput("timezone", workingConfig_.timezone, "text", false, RUNTIME_TIMEZONE_LEN - 1);
  sendStatic("<p class=\"hint\">Regu&#322;a okre&#347;laj&#261;ca lokalny czas pilota.<br>Domy&#347;lnie: "); sendStatic(ConfigDefaults::kTimezone);
  sendStatic("</p></span></div><div class=\"row\"><label class=\"key\" for=\"ntp_server\">Serwer NTP</label><span class=\"value\">");
  sendInput("ntp_server", workingConfig_.ntpServer, "text", false, RUNTIME_NTP_SERVER_LEN - 1);
  sendStatic("<p class=\"hint\">Serwer synchronizuj&#261;cy zegar pilota.<br>Domy&#347;lnie: "); sendStatic(ConfigDefaults::kNtpServer); sendStatic("</p></span></div></section>");
}

void ConfigPortal::sendRemoteSection() {
  const char* labels[] = {"Op&oacute;&#378;nienie przewijania", "Szybko&#347;&#263; przewijania", "Czas ekranu g&#322;o&#347;no&#347;ci", "Powtarzanie g&#322;o&#347;no&#347;ci", "Czas wyboru radia", "Minimalny czas ekranu &#322;&#261;czenia", "Czas do u&#347;pienia podczas odtwarzania", "Czas do u&#347;pienia po zatrzymaniu"};
  const char* names[] = {"scroll_delay", "scroll_step", "volume_timeout", "volume_repeat", "radio_menu_timeout", "radio_connecting_min", "deep_sleep_timeout", "stopped_timeout"};
  uint32_t* values[] = {&workingConfig_.scrollStartDelayMs, &workingConfig_.scrollStepMs, &workingConfig_.volumeScreenTimeoutMs, &workingConfig_.volumeRepeatMs, &workingConfig_.radioMenuTimeoutMs, &workingConfig_.radioConnectingMinMs, &workingConfig_.deepSleepTimeoutSec, &workingConfig_.stoppedTimeoutSec};
  const uint32_t minimums[] = {ConfigValidator::SCROLL_DELAY_MIN_MS, ConfigValidator::SCROLL_STEP_MIN_MS, ConfigValidator::VOLUME_TIMEOUT_MIN_MS, ConfigValidator::VOLUME_REPEAT_MIN_MS, ConfigValidator::RADIO_MENU_TIMEOUT_MIN_MS, ConfigValidator::RADIO_CONNECTING_MIN_MS, ConfigValidator::DEEP_SLEEP_TIMEOUT_MIN_SEC, ConfigValidator::STOPPED_TIMEOUT_MIN_SEC};
  const uint32_t maximums[] = {ConfigValidator::SCROLL_DELAY_MAX_MS, ConfigValidator::SCROLL_STEP_MAX_MS, ConfigValidator::VOLUME_TIMEOUT_MAX_MS, ConfigValidator::VOLUME_REPEAT_MAX_MS, ConfigValidator::RADIO_MENU_TIMEOUT_MAX_MS, ConfigValidator::RADIO_CONNECTING_MAX_MS, ConfigValidator::DEEP_SLEEP_TIMEOUT_MAX_SEC, ConfigValidator::STOPPED_TIMEOUT_MAX_SEC};
  const uint32_t defaults[] = {ConfigDefaults::kScrollStartDelayMs, ConfigDefaults::kScrollStepMs, ConfigDefaults::kVolumeScreenTimeoutMs, ConfigDefaults::kVolumeRepeatMs, ConfigDefaults::kRadioMenuTimeoutMs, ConfigDefaults::kRadioConnectingMinMs, ConfigDefaults::kDeepSleepTimeoutSec, ConfigDefaults::kStoppedTimeoutSec};
  const char* hints[] = {"Czas przed rozpocz&#281;ciem przewijania d&#322;ugiego tekstu.", "Odst&#281;p mi&#281;dzy krokami przewijania. Mniejsza warto&#347;&#263; = szybsze przewijanie.", "Jak d&#322;ugo po zmianie g&#322;o&#347;no&#347;ci pozostaje widoczny jej ekran.", "Odst&#281;p mi&#281;dzy zmianami g&#322;o&#347;no&#347;ci podczas przytrzymania przycisku.", "Czas bezczynno&#347;ci przed zamkni&#281;ciem menu wyboru radia.", "Najkr&oacute;tszy czas wy&#347;wietlania ekranu &#322;&#261;czenia z radiem.", "Czas bezczynno&#347;ci do deep sleep podczas odtwarzania. 0 = wy&#322;&#261;czone.", "Czas bezczynno&#347;ci do deep sleep po zatrzymaniu radia. 0 = wy&#322;&#261;czone."};
  sendStatic("<section class=\"card\"><h2>Pilot</h2>");
  char brightness[4]{}; snprintf(brightness, sizeof brightness, "%u", workingSystemSettings_.lcdBrightnessPercent);
  sendStatic("<div class=\"row\"><label class=\"key\" for=\"brightness\">Jasno&#347;&#263; LCD</label><span class=\"value\">");
  sendNumberInput("brightness", brightness, ConfigValidator::LCD_BRIGHTNESS_MIN_PERCENT, ConfigValidator::LCD_BRIGHTNESS_MAX_PERCENT);
  sendStatic("<span class=\"unit\">%</span><p class=\"hint\">Jasno&#347;&#263; normalnej pracy LCD.<br>Zakres: 0&ndash;100%.</p></span></div>");
  for (uint8_t i = 0; i < 8; ++i) {
    char value[11]{}; snprintf(value, sizeof value, "%lu", static_cast<unsigned long>(*values[i]));
    const char* unit = i < 6 ? "ms" : "s";
    sendStatic("<div class=\"row\"><label class=\"key\" for=\""); sendStatic(names[i]); sendStatic("\">"); sendStatic(labels[i]); sendStatic("</label><span class=\"value\">");
    sendNumberInput(names[i], value, minimums[i], maximums[i]); sendStatic("<span class=\"unit\">"); sendStatic(unit); sendStatic("</span><p class=\"hint\">"); sendStatic(hints[i]);
    sendStatic(" Zakres: "); sendUnsigned(minimums[i]); sendStatic("&ndash;"); sendUnsigned(maximums[i]); sendStatic(" "); sendStatic(unit); sendStatic(". Domy&#347;lnie: "); sendUnsigned(defaults[i]); sendStatic(" "); sendStatic(unit); sendStatic(".</p></span></div>");
  }
  sendStatic("<div class=\"row\"><label class=\"key\" for=\"usb_sleep_inhibit\">Nie usypiaj przy pod&#322;&#261;czonym USB</label><span class=\"value\">");
  sendCheckbox("usb_sleep_inhibit", workingSystemSettings_.usbSleepInhibit);
  sendStatic("<p class=\"hint\">Zaznaczone = zasilanie USB blokuje automatyczne usypianie.</p></span></div></section>");
}

void ConfigPortal::sendSystemSection() {
  sendStatic("<section class=\"card\"><h2>Ustawienia systemowe</h2><div class=\"row\"><label class=\"key\" for=\"serial_debug\">Diagnostyka Serial</label><span class=\"value\">");
  sendCheckbox("serial_debug", workingSystemSettings_.serialDebug);
  sendStatic("<p class=\"hint\">Wy&#347;wietla szczeg&oacute;&#322;owe logi diagnostyczne przez USB/UART. W&#322;&#261;cz tylko podczas test&oacute;w i diagnozowania problem&oacute;w.</p></span></div></section>");
  uint8_t mac[6]{}; WiFi.softAPmacAddress(mac);
  char macText[18]{}; snprintf(macText, sizeof macText, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  sendStatic("<section class=\"card\"><h2>Informacje systemowe</h2><div class=\"row\"><span class=\"key\">Wersja firmware</span><span class=\"value\">");
  sendStatic(FirmwareVersion::kString); sendStatic("</span></div><div class=\"row\"><span class=\"key\">MAC ESP32</span><span class=\"value\">"); sendStatic(macText);
  sendStatic("</span></div><div class=\"row\"><span class=\"key\">Adres IP Config AP</span><span class=\"value\">");
  const IPAddress ip = accessPoint_.ip(); for (uint8_t i = 0; i < 4; ++i) { if (i) sendStatic("."); sendUnsigned(ip[i]); }
  sendStatic("</span></div><div class=\"row\"><span class=\"key\">Wolna pami&#281;&#263; heap</span><span class=\"value\">"); sendUnsigned(ESP.getFreeHeap());
  sendStatic(" B</span></div><div class=\"row\"><span class=\"key\">Czas dzia&#322;ania</span><span class=\"value\">"); sendUnsigned(millis() / 1000); sendStatic(" s</span></div></section>");
  sendStatic("<section class=\"actions\"><p class=\"hint\"><strong>SPRAWD&#377; USTAWIENIA</strong> &mdash; Sprawdzenie i walidacja ustawie&#324; bez zapisu i bez restartu.<br><strong>ZAPISZ I URUCHOM PONOWNIE</strong> &mdash; Sprawdzenie, walidacja i zapis ustawie&#324;. Pilot zostanie uruchomiony ponownie.</p><p><button class=\"submit check\" type=\"submit\" formaction=\"/validate\">SPRAWD&#377; USTAWIENIA</button></p><p><button class=\"submit\" type=\"submit\" formaction=\"/save\">ZAPISZ I URUCHOM PONOWNIE</button></p></section>");
}

bool ConfigPortal::buildCandidate(RuntimeConfig &c, ConfigValidationError &e,
                                  uint8_t &r) {
  r = 0;
  if (!copyTextArgument("ssid", c.wifiSsid, sizeof c.wifiSsid, false,
                        ConfigValidationError::SSID_TOO_LONG, e) ||
      !copyTextArgument("password", c.wifiPass, sizeof c.wifiPass, true,
                        ConfigValidationError::WIFI_PASSWORD_TOO_LONG, e) ||
      !copyTextArgument("timezone", c.timezone, sizeof c.timezone, false,
                        ConfigValidationError::TIMEZONE_INVALID, e) ||
      !copyTextArgument("ntp_server", c.ntpServer, sizeof c.ntpServer, false,
                        ConfigValidationError::NTP_SERVER_INVALID, e))
    return false;
  c.useStaticIp = !server_.hasArg("dhcp");
  const char *n[] = {"static_ip", "gateway", "subnet", "dns"};
  char *t[] = {c.staticIp, c.gatewayIp, c.subnetMask, c.dns1Ip};
  ConfigValidationError es[] = {ConfigValidationError::INVALID_STATIC_IP,
                                ConfigValidationError::INVALID_GATEWAY,
                                ConfigValidationError::INVALID_SUBNET,
                                ConfigValidationError::INVALID_DNS1};
  for (uint8_t i = 0; i < 4; i++)
    if (!copyTextArgument(n[i], t[i], RUNTIME_IP_LEN, !c.useStaticIp, es[i], e))
      return false;
  strncpy(c.dns2Ip, c.dns1Ip, sizeof c.dns2Ip);
  c.dns2Ip[sizeof c.dns2Ip - 1] = '\0';
  for (uint8_t i = 0; i < YORADIO_MAX; i++) {
    r = i;
    auto &x = c.radios[i];
    char a[16], b[16], d[16], p[16];
    rn(a, sizeof a, i, "name");
    rn(b, sizeof b, i, "host");
    rn(d, sizeof d, i, "port");
    rn(p, sizeof p, i, "path");
    char en[16];
    rn(en, sizeof en, i, "enabled");
    x.enabled = server_.hasArg(en);
    if (x.enabled && x.id == 0) {
      uint8_t preferred = i + 1;
      bool used = false;
      for (uint8_t j = 0; j < YORADIO_MAX; ++j)
        if (j != i && c.radios[j].enabled && c.radios[j].id == preferred) used = true;
      if (!used) x.id = preferred;
      else for (uint8_t candidateId = 1; candidateId <= YORADIO_MAX; ++candidateId) {
        used = false;
        for (uint8_t j = 0; j < YORADIO_MAX; ++j)
          if (j != i && c.radios[j].enabled && c.radios[j].id == candidateId) used = true;
        if (!used) { x.id = candidateId; break; }
      }
    }
    if (!copyTextArgument(a, x.name, sizeof x.name, false,
                          ConfigValidationError::FORM_FIELD_MISSING, e) ||
        !copyTextArgument(b, x.host, sizeof x.host, false,
                          ConfigValidationError::FORM_FIELD_MISSING, e) ||
        !copyTextArgument(p, x.wsPath, sizeof x.wsPath, false,
                          ConfigValidationError::FORM_FIELD_MISSING, e))
      return false;
    uint32_t port = x.wsPort;
    if (!readNumberArgument(d, port, 0, 65535,
                            ConfigValidationError::RADIO_PORT_OUT_OF_RANGE, e))
      return false;
    x.wsPort = uint16_t(port);
  }
  const char *nn[] = {"volume_timeout",     "volume_repeat",
                      "scroll_delay",       "scroll_step",
                      "radio_menu_timeout", "radio_connecting_min",
                      "deep_sleep_timeout", "stopped_timeout"};
  uint32_t *vv[] = {&c.volumeScreenTimeoutMs, &c.volumeRepeatMs,
                    &c.scrollStartDelayMs,    &c.scrollStepMs,
                    &c.radioMenuTimeoutMs,    &c.radioConnectingMinMs,
                    &c.deepSleepTimeoutSec,   &c.stoppedTimeoutSec};
  ConfigValidationError ee[] = {
      ConfigValidationError::VOLUME_TIMEOUT_OUT_OF_RANGE,
      ConfigValidationError::VOLUME_REPEAT_OUT_OF_RANGE,
      ConfigValidationError::SCROLL_DELAY_OUT_OF_RANGE,
      ConfigValidationError::SCROLL_STEP_OUT_OF_RANGE,
      ConfigValidationError::RADIO_MENU_TIMEOUT_OUT_OF_RANGE,
      ConfigValidationError::RADIO_CONNECTING_TIMEOUT_OUT_OF_RANGE,
      ConfigValidationError::DEEP_SLEEP_TIMEOUT_OUT_OF_RANGE,
      ConfigValidationError::STOPPED_TIMEOUT_OUT_OF_RANGE};
  for (uint8_t i = 0; i < 8; i++)
    if (!readNumberArgument(nn[i], *vv[i], 0, UINT32_MAX, ee[i], e))
      return false;
  return true;
}
bool ConfigPortal::buildSystemSettings(SystemSettings& settings) {
  uint32_t brightness = settings.lcdBrightnessPercent;
  ConfigValidationError error = ConfigValidationError::NONE;
  if (!readNumberArgument("brightness", brightness,
                          ConfigValidator::LCD_BRIGHTNESS_MIN_PERCENT,
                          ConfigValidator::LCD_BRIGHTNESS_MAX_PERCENT,
                          ConfigValidationError::FORM_NUMBER_INVALID, error)) {
    return false;
  }
  settings.lcdBrightnessPercent = static_cast<uint8_t>(brightness);
  settings.serialDebug = server_.hasArg("serial_debug");
  settings.usbSleepInhibit = server_.hasArg("usb_sleep_inhibit");
  return true;
}

bool ConfigPortal::copyTextArgument(const char *n, char *t, size_t z, bool keep,
                                    ConfigValidationError longErr,
                                    ConfigValidationError &e) {
  if (!server_.hasArg(n)) {
    if (keep)
      return true;
    e = ConfigValidationError::FORM_FIELD_MISSING;
    return false;
  }
  String v = server_.arg(n);
  if (v.length() >= z) {
    e = longErr;
    return false;
  }
  if (keep && v.isEmpty())
    return true;
  memcpy(t, v.c_str(), v.length());
  t[v.length()] = '\0';
  return true;
}
bool ConfigPortal::readNumberArgument(const char *n, uint32_t &t, uint32_t lo,
                                      uint32_t hi, ConfigValidationError re,
                                      ConfigValidationError &e) {
  if (!server_.hasArg(n)) {
    e = ConfigValidationError::FORM_FIELD_MISSING;
    return false;
  }
  String v = server_.arg(n);
  char *end = nullptr;
  errno = 0;
  unsigned long x = strtoul(v.c_str(), &end, 10);
  if (v.isEmpty() || errno == ERANGE || end == v.c_str() || *end != '\0' ||
      x > UINT32_MAX || x < lo || x > hi) {
    e = re;
    return false;
  }
  t = uint32_t(x);
  return true;
}
const char *ConfigPortal::formatValidationError(const ValidationResult &result) const {
  const unsigned radio = static_cast<unsigned>(result.index) + 1;
  switch (result.error) {
  case ConfigValidationError::RADIO_NAME_MISSING:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Radio %u: nazwa jest pusta.", radio); return errorBuffer_;
  case ConfigValidationError::RADIO_HOST_MISSING:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Radio %u: host / IP jest pusty.", radio); return errorBuffer_;
  case ConfigValidationError::RADIO_PORT_OUT_OF_RANGE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Radio %u: port musi by&#263; w zakresie 1&ndash;65535.", radio); return errorBuffer_;
  case ConfigValidationError::RADIO_PATH_INVALID:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Radio %u: &#347;cie&#380;ka WebSocket musi zaczyna&#263; si&#281; od /.", radio); return errorBuffer_;
  case ConfigValidationError::RADIO_ID_INVALID:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Radio %u: ID jest nieprawid&#322;owe.", radio); return errorBuffer_;
  case ConfigValidationError::RADIO_ID_DUPLICATE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Radio %u: ID powtarza si&#281; w innym radiu.", radio); return errorBuffer_;
  case ConfigValidationError::SSID_MISSING: return "SSID Wi-Fi nie mo&#380;e by&#263; puste.";
  case ConfigValidationError::SSID_TOO_LONG: return "Sie&#263;: SSID jest nieprawid&#322;owe.";
  case ConfigValidationError::NO_ENABLED_RADIOS: return "Co najmniej jedno radio musi by&#263; w&#322;&#261;czone.";
  case ConfigValidationError::INVALID_STATIC_IP: return "Sie&#263;: statyczny adres IP jest nieprawid&#322;owy.";
  case ConfigValidationError::INVALID_GATEWAY: return "Sie&#263;: brama jest nieprawid&#322;owa.";
  case ConfigValidationError::INVALID_SUBNET: return "Sie&#263;: maska podsieci jest nieprawid&#322;owa.";
  case ConfigValidationError::INVALID_DNS1: return "Sie&#263;: DNS1 jest nieprawid&#322;owy.";
  case ConfigValidationError::INVALID_DNS2: return "Sie&#263;: DNS2 jest nieprawid&#322;owy.";
  case ConfigValidationError::TIMEZONE_INVALID: return "Czas: strefa czasowa jest wymagana.";
  case ConfigValidationError::NTP_SERVER_INVALID: return "Czas: serwer NTP jest wymagany.";
  case ConfigValidationError::VOLUME_TIMEOUT_OUT_OF_RANGE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Pilot: czas ekranu g&#322;o&#347;no&#347;ci musi by&#263; w zakresie %lu&ndash;%lu ms.", static_cast<unsigned long>(ConfigValidator::VOLUME_TIMEOUT_MIN_MS), static_cast<unsigned long>(ConfigValidator::VOLUME_TIMEOUT_MAX_MS)); return errorBuffer_;
  case ConfigValidationError::VOLUME_REPEAT_OUT_OF_RANGE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Pilot: powtarzanie g&#322;o&#347;no&#347;ci musi by&#263; w zakresie %lu&ndash;%lu ms.", static_cast<unsigned long>(ConfigValidator::VOLUME_REPEAT_MIN_MS), static_cast<unsigned long>(ConfigValidator::VOLUME_REPEAT_MAX_MS)); return errorBuffer_;
  case ConfigValidationError::SCROLL_DELAY_OUT_OF_RANGE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Pilot: op&oacute;&#378;nienie przewijania musi by&#263; w zakresie %lu&ndash;%lu ms.", static_cast<unsigned long>(ConfigValidator::SCROLL_DELAY_MIN_MS), static_cast<unsigned long>(ConfigValidator::SCROLL_DELAY_MAX_MS)); return errorBuffer_;
  case ConfigValidationError::SCROLL_STEP_OUT_OF_RANGE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Pilot: szybko&#347;&#263; przewijania musi by&#263; w zakresie %lu&ndash;%lu ms.", static_cast<unsigned long>(ConfigValidator::SCROLL_STEP_MIN_MS), static_cast<unsigned long>(ConfigValidator::SCROLL_STEP_MAX_MS)); return errorBuffer_;
  case ConfigValidationError::RADIO_MENU_TIMEOUT_OUT_OF_RANGE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Pilot: czas wyboru radia musi by&#263; w zakresie %lu&ndash;%lu ms.", static_cast<unsigned long>(ConfigValidator::RADIO_MENU_TIMEOUT_MIN_MS), static_cast<unsigned long>(ConfigValidator::RADIO_MENU_TIMEOUT_MAX_MS)); return errorBuffer_;
  case ConfigValidationError::RADIO_CONNECTING_TIMEOUT_OUT_OF_RANGE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Pilot: minimalny czas ekranu &#322;&#261;czenia musi by&#263; w zakresie %lu&ndash;%lu ms.", static_cast<unsigned long>(ConfigValidator::RADIO_CONNECTING_MIN_MS), static_cast<unsigned long>(ConfigValidator::RADIO_CONNECTING_MAX_MS)); return errorBuffer_;
  case ConfigValidationError::DEEP_SLEEP_TIMEOUT_OUT_OF_RANGE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Pilot: czas do u&#347;pienia musi by&#263; w zakresie %lu&ndash;%lu s.", static_cast<unsigned long>(ConfigValidator::DEEP_SLEEP_TIMEOUT_MIN_SEC), static_cast<unsigned long>(ConfigValidator::DEEP_SLEEP_TIMEOUT_MAX_SEC)); return errorBuffer_;
  case ConfigValidationError::STOPPED_TIMEOUT_OUT_OF_RANGE:
    snprintf(errorBuffer_, sizeof(errorBuffer_), "Pilot: czas do u&#347;pienia po zatrzymaniu musi by&#263; w zakresie %lu&ndash;%lu s.", static_cast<unsigned long>(ConfigValidator::STOPPED_TIMEOUT_MIN_SEC), static_cast<unsigned long>(ConfigValidator::STOPPED_TIMEOUT_MAX_SEC)); return errorBuffer_;
  default: return "Konfiguracja zawiera nieprawid&#322;ow&#261; warto&#347;&#263;.";
  }
}

const char *ConfigPortal::errorMessage(ConfigValidationError e, uint8_t) const {
  switch (e) {
  case ConfigValidationError::SSID_TOO_LONG:
    return "SSID jest za d&#322;ugie.";
  case ConfigValidationError::WIFI_PASSWORD_TOO_LONG:
    return "Has&#322;o Wi-Fi jest za d&#322;ugie.";
  case ConfigValidationError::INVALID_STATIC_IP:
    return "Nieprawid&#322;owy statyczny adres IP.";
  case ConfigValidationError::INVALID_GATEWAY:
    return "Nieprawid&#322;owa brama.";
  case ConfigValidationError::INVALID_SUBNET:
    return "Nieprawid&#322;owa maska podsieci.";
  case ConfigValidationError::INVALID_DNS1:
    return "Nieprawid&#322;owy DNS1.";
  case ConfigValidationError::INVALID_DNS2:
    return "Nieprawid&#322;owy DNS2.";
  case ConfigValidationError::TIMEZONE_INVALID:
    return "Strefa czasowa jest wymagana lub za d&#322;uga.";
  case ConfigValidationError::NTP_SERVER_INVALID:
    return "Serwer NTP jest wymagany lub za d&#322;ugi.";
  case ConfigValidationError::RADIO_NAME_MISSING:
    return "Radio: brak nazwy.";
  case ConfigValidationError::RADIO_HOST_MISSING:
    return "Radio: brak hosta.";
  case ConfigValidationError::RADIO_PORT_OUT_OF_RANGE:
    return "Radio: port poza zakresem.";
  case ConfigValidationError::RADIO_PATH_INVALID:
    return "&#346;cie&#380;ka WebSocket musi zaczyna&#263; si&#281; od /.";
  case ConfigValidationError::VOLUME_REPEAT_OUT_OF_RANGE:
    return "Czas powtarzania g&#322;o&#347;no&#347;ci jest poza zakresem.";
  case ConfigValidationError::FORM_FIELD_MISSING:
    return "Brakuje wymaganego pola formularza.";
  default:
    return "Warto&#347;&#263; konfiguracji jest nieprawid&#322;owa.";
  }
}
void ConfigPortal::sendEscaped(const char *v) {
  if (!v)
    return;
  for (; *v; v++) {
    switch (*v) {
    case '&':
      sendStatic("&amp;");
      break;
    case '<':
      sendStatic("&lt;");
      break;
    case '>':
      sendStatic("&gt;");
      break;
    case '\"':
      sendStatic("&quot;");
      break;
    case '\'':
      sendStatic("&#39;");
      break;
    default:
      renderBuffer_->concat(*v);
    }
  }
}
void ConfigPortal::sendUnsigned(uint32_t v) {
  char x[11]{};
  snprintf(x, sizeof x, "%lu", static_cast<unsigned long>(v));
  renderBuffer_->concat(x, strlen(x));
}
void ConfigPortal::sendStatic(const char *s) {
  renderBuffer_->concat(s, strlen(s));
}
void ConfigPortal::sendInput(const char* name, const char* value, const char* type,
                             bool disabled, size_t maxLength) {
  sendStatic("<input id=\"");
  sendStatic(name);
  sendStatic("\" name=\"");
  sendStatic(name);
  sendStatic("\" type=\"");
  sendStatic(type);
  sendStatic("\" value=\"");
  sendEscaped(value);
  sendStatic("\"");
  if (maxLength != 0) {
    sendStatic(" maxlength=\"");
    sendUnsigned(maxLength);
    sendStatic("\"");
  }
  if (disabled) sendStatic(" class=\"static-ip\" disabled");
  else if (!strcmp(name, "static_ip") || !strcmp(name, "gateway") || !strcmp(name, "subnet") ||
           !strcmp(name, "dns1") || !strcmp(name, "dns2") || !strcmp(name, "dns")) sendStatic(" class=\"static-ip\"");
  if (!strcmp(type, "password")) sendStatic(" autocomplete=\"new-password\"");
  else if (!strcmp(name, "ssid")) sendStatic(" autocomplete=\"off\"");
  sendStatic(">");
}
void ConfigPortal::sendNumberInput(const char* name, const char* value,
                                   uint32_t minimum, uint32_t maximum) {
  sendStatic("<input id=\"");
  sendStatic(name);
  sendStatic("\" name=\"");
  sendStatic(name);
  sendStatic("\" type=\"number\" inputmode=\"numeric\" step=\"1\" min=\"");
  sendUnsigned(minimum);
  sendStatic("\" max=\"");
  sendUnsigned(maximum);
  sendStatic("\" value=\"");
  sendEscaped(value);
  sendStatic("\">");
}
void ConfigPortal::sendCheckbox(const char *n, bool yes) {
  sendStatic("<input id=\"");
  sendStatic(n);
  sendStatic("\" name=\"");
  sendStatic(n);
  sendStatic("\" type=\"checkbox\"");
  if (yes)
    sendStatic(" checked");
  sendStatic(">");
}
