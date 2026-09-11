#include "HydroSenseControlPage.h"

using AquaCore::Web::WebResponseWriter;

HydroSenseControlPage::HydroSenseControlPage(
    const SystemStatus& status
)
    : status_(status)
{
}

const char* HydroSenseControlPage::route() const
{
    return "/control";
}

const char* HydroSenseControlPage::title() const
{
    return "Sterowanie";
}

void HydroSenseControlPage::render(
    WebResponseWriter& response
) const
{
    response.writeText(
        "<section class=\"card\">"
        "<h2>Stan</h2>"

        "<div class=\"row\">"
        "<span class=\"key\">Tryb</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status_.serviceMode
            ? "<span class=\"tag warn\">SERVICE</span>"
            : "<span class=\"tag ok\">NORMAL</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Pompa</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status_.pumpOn
            ? "<span class=\"tag warn\">WŁĄCZONA</span>"
            : "<span class=\"tag ok\">WYŁĄCZONA</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Automat</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        topupStateName(
            status_.topupState
        )
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">LOCKOUT</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status_.pumpLocked
            ? "<span class=\"tag err\">AKTYWNY</span>"
            : "<span class=\"tag ok\">BRAK</span>"
    );

    response.writeText(
        "</span></div>"

        "<div class=\"row\">"
        "<span class=\"key\">Buzzer</span>"
        "<span class=\"value\">"
    );

    response.writeText(
        status_.buzzerMuted
            ? "Wyciszony"
            : "Aktywny"
    );

    response.writeText(
        "</span></div>"
        "</section>"


        "<section class=\"card\">"
        "<h2>Sterowanie</h2>"

        "<div class=\"actions\">"

        "<button id=\"serviceButton\" "
        "type=\"button\" class=\""
    );

    response.writeText(
        status_.serviceMode
            ? "active"
            : ""
    );

    response.writeText(
        "\">"
    );

    response.writeText(
        status_.serviceMode
            ? "Wyłącz SERVICE"
            : "Włącz SERVICE"
    );

    response.writeText(
        "</button>"

        "<button id=\"muteButton\" "
        "type=\"button\">"
        "Wycisz buzzer"
        "</button>"

        "<button id=\"lockoutButton\" "
        "type=\"button\" class=\"danger\" "
    );

    if (!status_.pumpLocked)
    {
        response.writeText(
            "disabled"
        );
    }

    response.writeText(
        ">"
        "Reset LOCKOUT"
        "</button>"

        "</div>"

        "<div id=\"controlMessage\" "
        "class=\"notice\" hidden></div>"

        "<p class=\"hint\">"
        "Sterowanie ręczne pompą jest celowo niedostępne."
        "</p>"

        "</section>"


        "<script>"
        "(()=>{"

        "const msg=document.getElementById('controlMessage');"
        "const service=document.getElementById('serviceButton');"
        "const mute=document.getElementById('muteButton');"
        "const lockout=document.getElementById('lockoutButton');"

        "async function send(action){"

        "msg.hidden=false;"
        "msg.className='notice';"
        "msg.textContent='Wysyłanie...';"

        "try{"

        "const r=await fetch('/api/control',{"
        "method:'POST',"
        "headers:{'Content-Type':'text/plain;charset=UTF-8'},"
        "body:'action='+encodeURIComponent(action)"
        "});"

        "const text=await r.text();"

        "if(!r.ok){"
        "throw new Error(text||'Błąd');"
        "}"

        "msg.className='notice ok';"
        "msg.textContent='OK';"

        "setTimeout(()=>location.reload(),300);"

        "}catch(err){"

        "msg.className='notice err';"
        "msg.textContent=err.message||'Błąd';"

        "}"

        "}"

        "service.addEventListener('click',()=>send('service_toggle'));"
        "mute.addEventListener('click',()=>send('mute'));"

        "if(!lockout.disabled){"
        "lockout.addEventListener('click',()=>send('reset_lockout'));"
        "}"

        "})();"
        "</script>"
    );
}

const char*
HydroSenseControlPage::topupStateName(
    TopupController::State state
)
{
    switch (state)
    {
        case TopupController::State::Idle:
            return "IDLE";

        case TopupController::State::Waiting:
            return "WAITING";

        case TopupController::State::Pumping:
            return "PUMPING";

        case TopupController::State::Blocked:
            return "BLOCKED";

        case TopupController::State::Lockout:
            return "LOCKOUT";
    }

    return "UNKNOWN";
}