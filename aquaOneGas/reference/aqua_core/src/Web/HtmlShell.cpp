#include "AquaCore/Web/HtmlShell.h"

#include <cstring>

namespace AquaCore {
namespace Web {
namespace {

const char STYLESHEET[] =
    ":root{color-scheme:dark;--bg:#101827;--surface:#1f2937;"
    "--surface-alt:#111827;--text:#e5e7eb;--text-strong:#fff;"
    "--text-muted:#9ca3af;--accent:#65d46e;--accent-hover:#65d46e;"
    "--border:#374151;--border-strong:#4b5563;"
    "--success-bg:#14532d;--success:#bbf7d0;"
    "--warning-bg:#374151;--warning:#e5e7eb;"
    "--error-bg:#7f1d1d;--error:#fecaca;--disabled:.45}"
    "*{box-sizing:border-box}html{background:var(--bg)}"
    "body{margin:0;overflow-x:hidden;background:var(--bg);color:var(--text);"
    "font:15px/1.4 system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}"
    ".page{width:min(720px,calc(100% - 20px));margin-inline:auto}"
    ".site-header{padding:10px 0 5px}"
    "h1,h2,p{margin:0}h1{color:var(--text-strong);font-size:22px;"
    "line-height:1.15;font-weight:700}h2{color:var(--text-strong);"
    "font-size:16px;line-height:1.25;margin:0 0 8px}"
    ".sub,.hint,.muted,.meta{color:var(--text-muted)}"
    ".sub{font-size:13px}.hint{margin:4px 0 0;font-size:12px}"
    ".meta{margin-top:2px;font-size:12px}.header-line{display:flex;"
    "align-items:center;gap:7px;margin-top:2px}"
    ".nav-shell{width:min(720px,calc(100% - 20px));margin:auto;"
    "display:flex;gap:4px;flex-wrap:wrap;padding:3px 0 5px}"
    ".nav-shell a{min-height:34px;display:inline-flex;align-items:center;"
    "justify-content:center;flex:1 1 auto;white-space:nowrap;font-size:13px;"
    "color:var(--text);text-decoration:none;padding:5px 8px;"
    "border:1px solid var(--border);border-radius:8px;background:var(--surface)}"
    ".nav-shell a:hover,.nav-shell a:focus-visible{border-color:var(--border-strong);"
    "background:var(--border)}"
    ".nav-shell a.active{background:var(--success-bg);color:var(--success);"
    "border-color:var(--success-bg);font-weight:700}"
    "main.page{padding:4px 0 10px;min-height:50vh}"
    ".page-title{margin:0 0 5px;color:var(--text-muted);font-size:12px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:8px}"
    ".card{background:var(--surface);border:1px solid var(--border);"
    "border-radius:12px;padding:12px;margin:8px 0;min-width:0}"
    ".grid>.card{margin:0}"
    ".row{display:grid;grid-template-columns:minmax(118px,36%) minmax(0,1fr);"
    "gap:8px;padding:5px 0;border-top:1px solid var(--border);align-items:center;"
    "font-size:14px;line-height:1.3}"
    ".row:first-of-type{border-top:0}.key{color:var(--text-muted);font-size:13px}"
    ".value{min-width:0;overflow-wrap:anywhere;color:var(--text-strong)}"
    ".value strong,.value-strong{color:var(--text-strong)}"
    "button,select,input{font:inherit}select,input:not([type=range]){"
    "box-sizing:border-box;width:100%;min-height:36px;background:var(--surface-alt);"
    "color:var(--text-strong);border:1px solid var(--border-strong);"
    "border-radius:7px;padding:6px 8px}"
    "button{min-height:36px;padding:7px 9px;border:1px solid var(--border-strong);"
    "border-radius:8px;background:var(--border);color:var(--text-strong);"
    "font-size:14px;font-weight:700;cursor:pointer}"
    "button.primary{border-color:var(--accent);background:var(--accent);color:#102018}"
    "button.danger{border-color:var(--error-bg);background:var(--error-bg);color:var(--error)}"
    "button:hover{border-color:var(--accent);filter:brightness(1.08)}"
    "button:active{filter:brightness(.9)}button.active{background:var(--success-bg);"
    "border-color:var(--accent);color:var(--success)}"
    "button:disabled,input:disabled,select:disabled{opacity:var(--disabled);cursor:not-allowed}"
    "button:focus-visible,input:focus-visible,select:focus-visible,a:focus-visible{"
    "outline:2px solid var(--accent);outline-offset:2px}"
    "input[type=range]{width:100%;min-height:32px;margin:0;accent-color:var(--accent)}"
    ".actions{display:flex;gap:6px;flex-wrap:wrap;margin:8px 0 0}"
    ".actions button{flex:1 1 120px}"
    ".tag{display:inline-block;border-radius:999px;padding:1px 7px;"
    "background:var(--border);color:var(--text);font-size:12px;font-weight:700}"
    ".tag.ok,.notice.ok{background:var(--success-bg);color:var(--success)}"
    ".tag.warn,.notice.warn{background:var(--warning-bg);color:var(--warning)}"
    ".tag.err,.notice.err{background:var(--error-bg);color:var(--error)}"
    ".notice{border-radius:9px;padding:8px 10px;margin:8px 0}"
    ".status{color:var(--success);font-weight:700}"
    "pre{margin:0;white-space:pre-wrap;overflow-wrap:anywhere;color:var(--text);"
    "font:13px/1.5 ui-monospace,SFMono-Regular,Consolas,monospace}"
    "[hidden]{display:none!important}"
    "footer.page{margin-top:4px;padding:8px 0 10px;border-top:1px solid var(--border);"
    "color:var(--text-muted);font-size:12px;text-align:center}"
    "footer .device{color:var(--text-strong)}footer .core{color:var(--accent)}"
    "@media(max-width:460px){.page,.nav-shell{width:calc(100% - 16px)}"
    ".site-header{padding-top:8px}.nav-shell a{padding:4px 5px;font-size:12px}"
    ".grid{grid-template-columns:1fr}.card{padding:11px;margin:7px 0}"
    ".row{grid-template-columns:minmax(100px,36%) minmax(0,1fr);gap:6px}}"
    "@media(max-width:340px){.nav-shell a{flex:1 1 calc(50% - 4px)}}";

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
    DeviceIdentity fallback(
        "aqua-device",
        "Aqua Device",
        "unknown",
        "unknown"
    );
    const DeviceIdentity& identity = system != nullptr
        ? system->deviceIdentity()
        : fallback;
    const char* coreVersion = system != nullptr
        ? system->aquaCoreVersion()
        : AQUA_CORE_VERSION;
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
        "</title></head><body><header class=\"site-header page\"><h1>"
    ) && ok;
    ok = writeHtmlEscaped(response, identity.deviceName) && ok;
    ok = response.writeText(
        "</h1><div class=\"header-line\"><span class=\"sub\">"
    ) && ok;
    ok = writeHtmlEscaped(response, identity.deviceType) && ok;
    ok = response.writeText("</span><span class=\"tag ") && ok;
    const bool ready = system != nullptr && system->isReady();
    ok = response.writeText(ready ? "ok\">GOTOWY" : "err\">NIEDOSTEPNY") && ok;
    ok = response.writeText("</span></div><p class=\"meta\">Firmware ") && ok;
    ok = writeHtmlEscaped(response, identity.firmwareVersion) && ok;
    ok = response.writeText(" &middot; Aqua Core ") && ok;
    ok = writeHtmlEscaped(response, coreVersion) && ok;
    ok = response.writeText("</p></header>") && ok;
    ok = writeNavigation(response, config.navigationMask, currentPath) && ok;
    ok = response.writeText("<main class=\"page\"><p class=\"page-title\">") && ok;
    ok = writeHtmlEscaped(response, title) && ok;
    ok = response.writeText("</p>") && ok;

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
