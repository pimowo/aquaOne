#include "AquaCore/Web/HtmlShell.h"

#include <cstring>

namespace AquaCore {
namespace Web {
namespace {

const char STYLESHEET[] =
    ":root{color-scheme:dark;--bg:#0b1422;--surface:#121f31;--surface-2:#182940;"
    "--border:#29405c;--border-strong:#3b5a7d;--text:#dce7f3;--text-strong:#f7fbff;"
    "--text-muted:#93a8bd;--accent:#49c5c7;--accent-soft:#173d4d;--success:#6fd4b1;"
    "--success-soft:#143b3a;--warning:#e4b767;--warning-soft:#3b3120;--danger:#ed7d82;"
    "--danger-soft:#40242c;--disabled:.46}*{box-sizing:border-box}"
    "html{background:var(--bg)}body{margin:0;overflow-x:hidden;background:var(--bg);"
    "color:var(--text);font:14px/1.45 system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}"
    ".page{width:min(1160px,calc(100% - 40px));margin-inline:auto}h1,h2,p{margin:0}"
    "h1{color:var(--text-strong);font-size:23px;line-height:1.1;font-weight:680;letter-spacing:-.02em}"
    "h2{color:var(--text-strong);font-size:15px;line-height:1.25;margin:0 0 10px;font-weight:650}"
    ".site-header{padding:18px 0 12px}.header-main{display:flex;align-items:center;justify-content:space-between;gap:16px}"
    ".brand{display:flex;align-items:center;gap:11px;min-width:0}.brand-mark{display:grid;place-items:center;"
    "width:34px;height:34px;border:1px solid var(--accent);border-radius:10px;background:var(--accent-soft);"
    "color:var(--accent);font-size:12px;font-weight:800;letter-spacing:.04em}.brand-copy{min-width:0}"
    ".eyebrow{color:var(--accent);font-size:10px;font-weight:750;letter-spacing:.13em;text-transform:uppercase}"
    ".header-type{margin-top:2px;color:var(--text-muted);font-size:12px;overflow-wrap:anywhere}"
    ".header-meta{margin-top:4px;color:var(--text-muted);font-size:11px}.header-status{flex:0 0 auto}"
    ".sub,.hint,.muted,.meta{color:var(--text-muted)}.sub{font-size:13px}.hint{margin:5px 0 0;font-size:12px}"
    ".meta{margin-top:3px;font-size:11px}.nav-shell{width:min(1160px,calc(100% - 40px));margin:auto;"
    "display:flex;gap:6px;overflow-x:auto;padding:0 0 10px;scrollbar-width:thin}.nav-shell a{min-height:34px;"
    "display:inline-flex;align-items:center;justify-content:center;flex:0 0 auto;white-space:nowrap;font-size:12px;"
    "color:var(--text-muted);text-decoration:none;padding:6px 11px;border:1px solid var(--border);border-radius:8px;background:var(--surface)}"
    ".nav-shell a:hover{border-color:var(--border-strong);color:var(--text-strong);background:var(--surface-2)}"
    ".nav-shell a.active{background:var(--accent-soft);color:var(--accent);border-color:var(--accent);font-weight:750}"
    "main.page{padding:5px 0 16px;min-height:50vh}.page-heading{margin:2px 0 10px}.page-title{margin-top:2px;color:var(--text-strong);font-size:20px;font-weight:650}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:12px}.card{background:var(--surface);"
    "border:1px solid var(--border);border-radius:11px;padding:14px;margin:12px 0;min-width:0}.grid>.card{margin:0}"
    ".row{display:grid;grid-template-columns:minmax(118px,38%) minmax(0,1fr);gap:10px;padding:7px 0;border-top:1px solid var(--border);"
    "align-items:center;font-size:13px;line-height:1.35}.row:first-of-type{border-top:0}.key{color:var(--text-muted);font-size:12px}"
    ".value{min-width:0;overflow-wrap:anywhere;color:var(--text-strong);text-align:right}.value strong,.value-strong{color:var(--text-strong)}"
    "button,select,input{font:inherit}select,input:not([type=range]){box-sizing:border-box;width:100%;min-height:38px;"
    "background:var(--bg);color:var(--text-strong);border:1px solid var(--border-strong);border-radius:8px;padding:7px 9px}"
    "button{min-height:38px;padding:7px 10px;border:1px solid var(--border-strong);border-radius:8px;background:var(--surface-2);"
    "color:var(--text-strong);font-size:13px;font-weight:700;cursor:pointer}button.primary{border-color:var(--accent);background:var(--accent);color:var(--bg)}"
    "button.danger{border-color:var(--danger);background:var(--danger-soft);color:var(--danger)}button:hover{border-color:var(--accent);filter:brightness(1.08)}"
    "button:active{filter:brightness(.92)}button.active{background:var(--accent-soft);border-color:var(--accent);color:var(--accent)}"
    "button:disabled,input:disabled,select:disabled{opacity:var(--disabled);cursor:not-allowed}button:focus-visible,input:focus-visible,select:focus-visible,a:focus-visible{outline:2px solid var(--accent);outline-offset:2px}"
    "input[type=range]{width:100%;min-height:34px;margin:0;accent-color:var(--accent)}.actions{display:flex;gap:7px;flex-wrap:wrap;margin:10px 0 0}.actions button{flex:1 1 120px}"
    ".tag{display:inline-block;border:1px solid var(--border-strong);border-radius:999px;padding:2px 8px;background:var(--surface-2);color:var(--text);"
    "font-size:11px;font-weight:750;letter-spacing:.02em}.tag.ok,.notice.ok{border-color:var(--success);background:var(--success-soft);color:var(--success)}"
    ".tag.warn,.notice.warn{border-color:var(--warning);background:var(--warning-soft);color:var(--warning)}.tag.err,.notice.err{border-color:var(--danger);background:var(--danger-soft);color:var(--danger)}"
    ".notice{border:1px solid var(--border);border-radius:9px;padding:9px 10px;margin:10px 0}.status{color:var(--success);font-weight:700}"
    "pre{margin:0;white-space:pre-wrap;overflow-wrap:anywhere;color:var(--text);font:12px/1.5 ui-monospace,SFMono-Regular,Consolas,monospace}"
    "[hidden]{display:none!important}footer.page{margin-top:6px;padding:11px 0 14px;border-top:1px solid var(--border);color:var(--text-muted);font-size:11px;text-align:center}"
    "footer .device{color:var(--text-strong)}footer .core{color:var(--accent)}@media(max-width:680px){.page,.nav-shell{width:calc(100% - 24px)}"
    ".site-header{padding-top:13px}.grid{grid-template-columns:1fr}.card{padding:12px;margin:10px 0}.row{grid-template-columns:minmax(96px,38%) minmax(0,1fr);gap:8px}"
    ".nav-shell{padding-bottom:8px}.page-title{font-size:19px}}@media(max-width:380px){.header-main{align-items:flex-start}.header-status{display:none}.brand-mark{width:31px;height:31px}}";

struct NavigationDefinition {
    NavigationSection section;
    const char* label;
    const char* path;
};

const NavigationDefinition NAVIGATION[] = {
    {NavigationSection::Dashboard, "Dashboard", "/"},
    {NavigationSection::Control, "Sterowanie", "/control"},
    {NavigationSection::Automation, "Automation", "/automation"},
    {NavigationSection::Settings, "Settings", "/settings"},
    {NavigationSection::Diagnostics, "Diagnostyka", "/diagnostics"},
    {NavigationSection::System, "System", "/system"}
};

bool writeNavigation(
    WebResponseWriter& response,
    uint32_t mask,
    const char* currentPath
) {
    bool ok = response.writeText(
        "<nav class=\"nav-shell\" aria-label=\"Primary\">"
    );
    for (const NavigationDefinition& item : NAVIGATION) {
        if ((mask & navigationSectionMask(item.section)) == 0U) {
            continue;
        }

        const bool active =
            currentPath != nullptr &&
            std::strcmp(item.path, currentPath) == 0;

        ok = response.writeText(active
            ? "<a class=\"active\" aria-current=\"page\" href=\""
            : "<a href=\"") && ok;
        ok = response.writeText(item.path) && ok;
        ok = response.writeText("\">") && ok;
        ok = response.writeText(item.label) && ok;
        ok = response.writeText("</a>") && ok;
    }
    return response.writeText("</nav>") && ok;
}

} // namespace

bool HtmlShell::render(
    WebResponseWriter& response,
    const SystemService* system,
    const WebConfig& config,
    const char* pageTitle,
    const WebPageProvider* page
) {
    WebShellInfo info {};
    const DeviceIdentity fallback(
        "aqua-device",
        "Aqua Device",
        "unknown",
        "unknown"
    );
    info.identity = system != nullptr
        ? system->deviceIdentity()
        : fallback;
    std::strncpy(
        info.aquaCoreVersion,
        system != nullptr ? system->aquaCoreVersion() : AQUA_CORE_VERSION,
        sizeof(info.aquaCoreVersion) - 1U
    );
    info.ready = system != nullptr && system->isReady();
    return render(response, info, config, pageTitle, page);
}

bool HtmlShell::render(
    WebResponseWriter& response,
    const WebShellInfo& info,
    const WebConfig& config,
    const char* pageTitle,
    const WebPageProvider* page
) {
    const DeviceIdentity& identity = info.identity;
    const char* coreVersion = info.aquaCoreVersion;
    const char* title =
        pageTitle != nullptr ? pageTitle : "Dashboard";
    const char* currentPath =
        page != nullptr ? page->route() : "/";

    bool ok = response.beginResponse(200U, ContentType::Html);
    ok = response.writeText(
        "<!doctype html><html lang=\"pl\"><head>"
        "<meta charset=\"utf-8\"><meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\">"
        "<link rel=\"stylesheet\" href=\"/assets/aqua.css\"><title>"
    ) && ok;
    ok = writeHtmlEscaped(response, identity.deviceName) && ok;
    ok = response.writeText(" - ") && ok;
    ok = writeHtmlEscaped(response, title) && ok;
    ok = response.writeText(
        "</title></head><body><header class=\"site-header page\">"
        "<div class=\"header-main\"><div class=\"brand\">"
        "<span class=\"brand-mark\" aria-hidden=\"true\">A1</span>"
        "<div class=\"brand-copy\"><p class=\"eyebrow\">aquaOne</p><h1>"
    ) && ok;
    ok = writeHtmlEscaped(response, identity.deviceName) && ok;
    ok = response.writeText("</h1><p class=\"header-type\">") && ok;
    ok = writeHtmlEscaped(response, identity.deviceType) && ok;
    ok = response.writeText("</p><p class=\"header-meta\">Firmware ") && ok;
    ok = writeHtmlEscaped(response, identity.firmwareVersion) && ok;
    ok = response.writeText(" &middot; Aqua Core ") && ok;
    ok = writeHtmlEscaped(response, coreVersion) && ok;
    ok = response.writeText(
        "</p></div></div><span class=\"tag header-status "
    ) && ok;
    const bool ready = info.ready;
    ok = response.writeText(ready ? "ok\">GOTOWY" : "err\">NIEDOSTEPNY") && ok;
    ok = response.writeText("</span></div></header>") && ok;
    ok = writeNavigation(response, config.navigationMask, currentPath) && ok;
    ok = response.writeText(
        "<main class=\"page\"><div class=\"page-heading\">"
        "<p class=\"eyebrow\">Panel urz&#261;dzenia</p><p class=\"page-title\">"
    ) && ok;
    ok = writeHtmlEscaped(response, title) && ok;
    ok = response.writeText("</p></div>") && ok;

    if (page != nullptr) {
        page->render(response);
    } else {
        ok = response.writeText(
            "<div class=\"grid\"><section class=\"card\"><h2>Device</h2>"
            "<div class=\"row\"><span class=\"key\">Hardware</span>"
            "<span class=\"value\">"
        ) && ok;
        ok = writeHtmlEscaped(response, identity.hardwareVariant) && ok;
        ok = response.writeText(
            "</span></div></section><section class=\"card\"><h2>Status</h2>"
            "<div class=\"row\"><span class=\"key\">System</span>"
            "<span class=\"value\"><span class=\"tag "
        ) && ok;
        ok = response.writeText(ready
            ? "ok\">Ready"
            : "err\">Unavailable") && ok;
        ok = response.writeText(
            "</span></span></div></section></div>"
        ) && ok;
    }

    ok = response.writeText(
        "</main><footer class=\"page\"><span class=\"device\">"
    ) && ok;
    ok = writeHtmlEscaped(response, identity.deviceName) && ok;
    ok = response.writeText(" </span>&bull; FW ") && ok;
    ok = writeHtmlEscaped(response, identity.firmwareVersion) && ok;
    ok = response.writeText(" &bull; <span class=\"core\">Aqua Core ") && ok;
    ok = writeHtmlEscaped(response, coreVersion) && ok;
    ok = response.writeText("</span></footer></body></html>") && ok;
    return response.endResponse() && ok;
}

const char* HtmlShell::stylesheet() {
    return STYLESHEET;
}

size_t HtmlShell::stylesheetLength() {
    return std::strlen(STYLESHEET);
}

} // namespace Web
} // namespace AquaCore
