#include "HydroSenseSettingsPage.h"

#include <cstdio>

#include <AquaCore/Web/WebTypes.h>

using AquaCore::Web::WebResponseWriter;
using AquaCore::Web::writeHtmlEscaped;

HydroSenseSettingsPage::HydroSenseSettingsPage(
    const HydroSenseConfig& config
)
    : config_(config)
{
}

const char* HydroSenseSettingsPage::route() const
{
    return "/settings";
}

const char* HydroSenseSettingsPage::title() const
{
    return "Ustawienia";
}

void HydroSenseSettingsPage::render(
    WebResponseWriter& response
) const
{
    char number[32];

    response.writeText(
        "<form id=\"settingsForm\">"

        "<section class=\"card\">"
        "<h2>Pływak akwarium</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Aktywny stanem LOW</span>"
        "<span class=\"value\">"
        "<input type=\"checkbox\" "
        "name=\"floatActiveLow\" "
        "value=\"1\" "
    );

    writeChecked(
        response,
        config_.floatActiveLow
    );

    response.writeText(
        ">"
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Wewnętrzny pull-up</span>"
        "<span class=\"value\">"
        "<input type=\"checkbox\" "
        "name=\"floatUsePullup\" "
        "value=\"1\" "
    );

    writeChecked(
        response,
        config_.floatUsePullup
    );

    response.writeText(
        ">"
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Debounce [ms]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" "
        "name=\"floatDebounceMs\" "
        "min=\"0\" max=\"5000\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%lu",
        static_cast<unsigned long>(
            config_.floatDebounceMs
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"
        "</section>"


        "<section class=\"card\">"
        "<h2>Czujnik poziomu RO</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Minimalna odległość [cm]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" step=\"0.1\" "
        "name=\"ultrasonicMinDistanceCm\" "
        "value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f",
        static_cast<double>(
            config_.ultrasonicMinDistanceCm
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Maksymalna odległość [cm]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" step=\"0.1\" "
        "name=\"ultrasonicMaxDistanceCm\" "
        "value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f",
        static_cast<double>(
            config_.ultrasonicMaxDistanceCm
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Timeout [us]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" "
        "name=\"ultrasonicTimeoutUs\" "
        "min=\"1000\" max=\"100000\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%lu",
        static_cast<unsigned long>(
            config_.ultrasonicTimeoutUs
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"
        "</section>"


        "<section class=\"card\">"
        "<h2>Zbiornik RO</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Odległość PUSTY [cm]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" step=\"0.1\" "
        "name=\"tankEmptyDistanceCm\" "
        "value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f",
        static_cast<double>(
            config_.tankEmptyDistanceCm
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Odległość PEŁNY [cm]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" step=\"0.1\" "
        "name=\"tankFullDistanceCm\" "
        "value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f",
        static_cast<double>(
            config_.tankFullDistanceCm
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Interwał próbki [ms]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" "
        "name=\"tankSampleIntervalMs\" "
        "min=\"20\" max=\"10000\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%lu",
        static_cast<unsigned long>(
            config_.tankSampleIntervalMs
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Serie do błędu</span>"
        "<span class=\"value\">"
        "<input type=\"number\" "
        "name=\"tankMaxFailedSeries\" "
        "min=\"1\" max=\"100\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%u",
        static_cast<unsigned>(
            config_.tankMaxFailedSeries
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"
        "</section>"


        "<section class=\"card\">"
        "<h2>Rezerwa wody RO</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">LOW [%]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" step=\"0.1\" "
        "name=\"reserveLowPercent\" "
        "min=\"0\" max=\"100\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f",
        static_cast<double>(
            config_.reserveLowPercent
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">CRITICAL [%]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" step=\"0.1\" "
        "name=\"reserveCriticalPercent\" "
        "min=\"0\" max=\"100\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f",
        static_cast<double>(
            config_.reserveCriticalPercent
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Histereza [%]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" step=\"0.1\" "
        "name=\"reserveHysteresisPercent\" "
        "min=\"0\" max=\"20\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%.1f",
        static_cast<double>(
            config_.reserveHysteresisPercent
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"
        "</section>"


        "<section class=\"card\">"
        "<h2>Automatyczna dolewka</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Opóźnienie startu [ms]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" "
        "name=\"topupStartDelayMs\" "
        "min=\"0\" max=\"600000\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%lu",
        static_cast<unsigned long>(
            config_.topupStartDelayMs
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Maks. praca pompy [ms]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" "
        "name=\"topupMaxPumpRuntimeMs\" "
        "min=\"1000\" max=\"3600000\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%lu",
        static_cast<unsigned long>(
            config_.topupMaxPumpRuntimeMs
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"

        "<p class=\"hint\">"
        "Po przekroczeniu maksymalnego czasu pracy "
        "pompa przechodzi w LOCKOUT."
        "</p>"

        "</section>"


        "<section class=\"card\">"
        "<h2>Wi-Fi STA</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Włącz STA</span>"
        "<span class=\"value\">"
        "<input type=\"checkbox\" "
        "name=\"staEnabled\" value=\"1\" "
    );

    writeChecked(
        response,
        config_.wifiStaEnabled
    );

    response.writeText(
        ">"
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">SSID</span>"
        "<span class=\"value\">"
        "<input type=\"text\" "
        "name=\"ssid\" maxlength=\"32\" value=\""
    );

    writeValue(
        response,
        config_.wifiSsid
    );

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Hasło</span>"
        "<span class=\"value\">"
        "<input type=\"password\" "
        "name=\"password\" maxlength=\"64\" value=\""
    );

    writeValue(
        response,
        config_.wifiPassword
    );

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Hostname</span>"
        "<span class=\"value\">"
        "<input type=\"text\" "
        "name=\"hostname\" maxlength=\"32\" value=\""
    );

    writeValue(
        response,
        config_.wifiHostname
    );

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Auto reconnect</span>"
        "<span class=\"value\">"
        "<input type=\"checkbox\" "
        "name=\"autoReconnect\" value=\"1\" "
    );

    writeChecked(
        response,
        config_.wifiAutoReconnect
    );

    response.writeText(
        ">"
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Reconnect [ms]</span>"
        "<span class=\"value\">"
        "<input type=\"number\" "
        "name=\"reconnectMs\" "
        "min=\"1000\" max=\"600000\" value=\""
    );

    std::snprintf(
        number,
        sizeof(number),
        "%lu",
        static_cast<unsigned long>(
            config_.wifiReconnectIntervalMs
        )
    );

    response.writeText(number);

    response.writeText(
        "\"></span></div>"
        "</section>"


        "<section class=\"card\">"
        "<h2>Access Point</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Włącz AP</span>"
        "<span class=\"value\">"
        "<input type=\"checkbox\" "
        "name=\"apEnabled\" value=\"1\" "
    );

    writeChecked(
        response,
        config_.wifiApEnabled
    );

    response.writeText(
        ">"
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">SSID AP</span>"
        "<span class=\"value\">"
        "<input type=\"text\" "
        "name=\"apSsid\" maxlength=\"32\" value=\""
    );

    writeValue(
        response,
        config_.wifiApSsid
    );

    response.writeText(
        "\"></span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Hasło AP</span>"
        "<span class=\"value\">"
        "<input type=\"password\" "
        "name=\"apPassword\" maxlength=\"64\" value=\""
    );

    writeValue(
        response,
        config_.wifiApPassword
    );

    response.writeText(
        "\"></span></div>"
        "</section>"


        "<section class=\"card\">"
        "<h2>Zapis konfiguracji</h2>"

        "<div class=\"actions\">"
        "<button class=\"primary\" type=\"submit\">"
        "Zapisz ustawienia"
        "</button>"
        "</div>"

        "<div id=\"saveMessage\" "
        "class=\"notice\" hidden></div>"

        "<p class=\"hint\">"
        "Po poprawnym zapisie HydroSense wykona restart."
        "</p>"

        "</section>"

        "</form>"


        "<script>"
        "(()=>{"

        "const form="
        "document.getElementById('settingsForm');"

        "const msg="
        "document.getElementById('saveMessage');"

        "form.addEventListener('submit',async e=>{"

        "e.preventDefault();"

        "const fd=new FormData(form);"
        "const params=new URLSearchParams();"

        "for(const [k,v] of fd.entries()){"
        "params.append(k,v);"
        "}"

        "msg.hidden=false;"
        "msg.className='notice';"
        "msg.textContent='Zapisywanie...';"

        "try{"

        "const r=await fetch('/api/settings',{"
        "method:'POST',"
        "headers:{"
        "'Content-Type':'text/plain;charset=UTF-8'"
        "},"
        "body:params.toString()"
        "});"

        "const text=await r.text();"

        "if(!r.ok){"
        "throw new Error(text||'Blad zapisu');"
        "}"

        "msg.className='notice ok';"
        "msg.textContent="
        "'Ustawienia zapisane. HydroSense uruchamia sie ponownie...';"

        "}catch(err){"

        "msg.className='notice err';"
        "msg.textContent="
        "err.message||'Blad zapisu';"

        "}"

        "});"

        "})();"
        "</script>"
    );
}

void HydroSenseSettingsPage::writeChecked(
    WebResponseWriter& response,
    bool value
)
{
    if (value)
    {
        response.writeText("checked");
    }
}

void HydroSenseSettingsPage::writeValue(
    WebResponseWriter& response,
    const char* value
)
{
    writeHtmlEscaped(
        response,
        value != nullptr
            ? value
            : ""
    );
}