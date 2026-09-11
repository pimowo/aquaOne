#include "LumaPages.h"

namespace LumaSense {
namespace Web {

const char* DashboardPage::route() const {
    return "/";
}

const char* DashboardPage::title() const {
    return "Dashboard";
}

void DashboardPage::render(
    AquaCore::Web::WebResponseWriter& response
) const {
    response.writeText(R"HTML(
<style>
.overview .value{text-align:right}.channel-row{grid-template-columns:52px minmax(0,1fr)}.channel-values{display:flex;justify-content:flex-end;gap:16px;white-space:nowrap;color:var(--text-muted);font-size:12px}.channel-values b{margin-left:3px;color:var(--text-strong);font-size:14px}.channel-values .final b{color:var(--accent)}@media(max-width:460px){.overview .value{text-align:right}.channel-row{grid-template-columns:42px minmax(0,1fr)}.channel-values{gap:10px}}
</style>
<div class="grid overview">
<section class="card"><h2>Stan lampy</h2>
<div class="row"><span class="key">Profil</span><strong class="value" id="profile">-</strong></div>
<div class="row"><span class="key">Tryb</span><span class="value"><span class="tag" id="mode">-</span></span></div>
<div class="row"><span class="key">Dzie&#324; / noc</span><span class="value"><span class="tag" id="day">-</span></span></div>
<div class="row"><span class="key">Czas</span><strong class="value" id="time">-</strong></div>
<div class="row"><span class="key">Stan czasu</span><span class="value"><span class="tag" id="timeValid">-</span></span></div>
<div class="row"><span class="key">Limit</span><strong class="value" id="limit">-</strong></div>
</section>
<section class="card"><h2>Sie&#263;</h2>
<div class="row"><span class="key">Wi-Fi</span><span class="value"><span class="tag" id="wifi">-</span></span></div>
<div class="row"><span class="key">IP</span><strong class="value" id="ip">-</strong></div>
<div class="row"><span class="key">RSSI</span><strong class="value" id="rssi">-</strong></div>
<div class="row"><span class="key">Health</span><span class="value"><span class="tag" id="health">-</span></span></div>
</section>
</div>
)HTML");
    response.writeText(R"HTML(
<section class="card" data-dashboard-section="channels"><h2>Kana&#322;y</h2>
<div class="row channel-row" data-channel="1"><strong class="key">CH1</strong><span class="value channel-values"><span>&#379;&#261;dane <b id="req0">0.0%</b></span><span class="final">Ko&#324;cowe <b id="ch0">0.0%</b></span></span></div>
<div class="row channel-row" data-channel="2"><strong class="key">CH2</strong><span class="value channel-values"><span>&#379;&#261;dane <b id="req1">0.0%</b></span><span class="final">Ko&#324;cowe <b id="ch1">0.0%</b></span></span></div>
<div class="row channel-row" data-channel="3"><strong class="key">CH3</strong><span class="value channel-values"><span>&#379;&#261;dane <b id="req2">0.0%</b></span><span class="final">Ko&#324;cowe <b id="ch2">0.0%</b></span></span></div>
<div class="row channel-row" data-channel="4"><strong class="key">CH4</strong><span class="value channel-values"><span>&#379;&#261;dane <b id="req3">0.0%</b></span><span class="final">Ko&#324;cowe <b id="ch3">0.0%</b></span></span></div>
<div class="row channel-row" data-channel="5"><strong class="key">CH5</strong><span class="value channel-values"><span>&#379;&#261;dane <b id="req4">0.0%</b></span><span class="final">Ko&#324;cowe <b id="ch4">0.0%</b></span></span></div>
<div class="row channel-row" data-channel="6"><strong class="key">CH6</strong><span class="value channel-values"><span>&#379;&#261;dane <b id="req5">0.0%</b></span><span class="final">Ko&#324;cowe <b id="ch5">0.0%</b></span></span></div>
<div class="row channel-row" data-channel="7"><strong class="key">CH7</strong><span class="value channel-values"><span>&#379;&#261;dane <b id="req6">0.0%</b></span><span class="final">Ko&#324;cowe <b id="ch6">0.0%</b></span></span></div>
<div class="row channel-row" data-channel="8"><strong class="key">CH8</strong><span class="value channel-values"><span>&#379;&#261;dane <b id="req7">0.0%</b></span><span class="final">Ko&#324;cowe <b id="ch7">0.0%</b></span></span></div>
</section>
)HTML");
    response.writeText(R"HTML(
<script>(()=>{const e=id=>document.getElementById(id);const tag=(id,value,tone)=>{const n=e(id);n.textContent=value;n.className='tag'+(tone?' '+tone:'')};const healthTone=v=>v==='ok'?'ok':v==='error'?'err':v==='warning'?'warn':'';async function refresh(){try{const r=await fetch('/api/lumasense/status',{cache:'no-store'});if(!r.ok)return;const s=await r.json();e('profile').textContent=s.activeProfile+' - '+s.activeProfileName;tag('mode',s.mode,s.mode==='OFF'?'err':s.mode==='NORMAL'?'ok':'warn');tag('day',s.dayState,s.dayState==='DAY'?'ok':'');e('time').textContent=s.localTime;tag('timeValid',s.timeValid?'VALID':'INVALID',s.timeValid?'ok':'err');e('limit').textContent=s.globalPowerLimit+'%';tag('wifi',s.wifi.state,s.wifi.connected?'ok':s.wifi.state==='connecting'?'warn':'err');e('ip').textContent=s.wifi.ip;e('rssi').textContent=s.wifi.rssi+' dBm';tag('health',s.overallHealth,healthTone(s.overallHealth));s.requestedLevels.forEach((v,i)=>e('req'+i).textContent=v.toFixed(1)+'%');s.finalLevels.forEach((v,i)=>e('ch'+i).textContent=v.toFixed(1)+'%')}catch(_){tag('wifi','unavailable','err')}}refresh();setInterval(refresh,1500)})();</script>
)HTML");
}

const char* ControlPage::route() const {
    return "/control";
}

const char* ControlPage::title() const {
    return "Sterowanie";
}

void ControlPage::render(
    AquaCore::Web::WebResponseWriter& response
) const {
    response.writeText(R"HTML(
<style>
.control-card>.hint{margin-bottom:6px}.mode-state{display:flex;align-items:center;justify-content:space-between;gap:8px}.mode-state h2{margin:0}.mode-actions{display:grid;grid-template-columns:repeat(3,minmax(0,1fr))}.slider-row{grid-template-columns:40px minmax(0,1fr)}.slider-row .value{display:grid;grid-template-columns:minmax(0,1fr) 48px;gap:6px;align-items:center}.slider-row output{text-align:right;color:var(--text-strong);font-size:13px;font-weight:700}.manual-actions{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr)}.manual-actions button:first-child{background:var(--accent);border-color:var(--accent);color:#102018}@media(max-width:460px){.slider-row{grid-template-columns:34px minmax(0,1fr)}.slider-row .value{grid-template-columns:minmax(0,1fr) 44px}.manual-actions{grid-template-columns:1fr}}
</style>
<section class="card control-card"><h2>Profil</h2>
<div class="row"><label class="key" for="profile">Aktywny</label><span class="value"><select id="profile"><option value="1">Profil 1</option><option value="2">Profil 2</option><option value="3">Profil 3</option><option value="4">Profil 4</option><option value="5">Profil 5</option></select></span></div>
<p class="hint">Wyb&oacute;r jest zapisywany w pami&#281;ci urz&#261;dzenia.</p>
</section>
<section class="card control-card"><div class="mode-state"><h2>Tryb pracy</h2><span class="tag" id="currentMode">-</span></div>
<div class="actions mode-actions"><button type="button" data-mode="NORMAL">NORMAL</button><button type="button" data-mode="SERVICE">SERVICE</button><button type="button" class="danger" data-mode="OFF">OFF</button></div>
</section>
<section class="card control-card"><h2>MANUAL</h2>
<div class="row slider-row"><label class="key" for="m0">CH1</label><span class="value"><input id="m0" type="range" min="0" max="100" value="0"><output id="o0">0%</output></span></div>
<div class="row slider-row"><label class="key" for="m1">CH2</label><span class="value"><input id="m1" type="range" min="0" max="100" value="0"><output id="o1">0%</output></span></div>
<div class="row slider-row"><label class="key" for="m2">CH3</label><span class="value"><input id="m2" type="range" min="0" max="100" value="0"><output id="o2">0%</output></span></div>
<div class="row slider-row"><label class="key" for="m3">CH4</label><span class="value"><input id="m3" type="range" min="0" max="100" value="0"><output id="o3">0%</output></span></div>
<div class="row slider-row"><label class="key" for="m4">CH5</label><span class="value"><input id="m4" type="range" min="0" max="100" value="0"><output id="o4">0%</output></span></div>
<div class="row slider-row"><label class="key" for="m5">CH6</label><span class="value"><input id="m5" type="range" min="0" max="100" value="0"><output id="o5">0%</output></span></div>
<div class="row slider-row"><label class="key" for="m6">CH7</label><span class="value"><input id="m6" type="range" min="0" max="100" value="0"><output id="o6">0%</output></span></div>
<div class="row slider-row"><label class="key" for="m7">CH8</label><span class="value"><input id="m7" type="range" min="0" max="100" value="0"><output id="o7">0%</output></span></div>
<div class="row"><label class="key" for="timeout">Limit czasu</label><span class="value"><select id="timeout"><option value="0">Bez limitu</option><option value="15">15 minut</option><option value="30">30 minut</option><option value="60">60 minut</option></select></span></div>
<div class="actions manual-actions"><button type="button" id="manual">W&#322;&#261;cz / aktualizuj MANUAL</button><button type="button" id="exitManual">Wyjd&#378; z MANUAL</button></div>
</section>
<p id="message" class="notice message" role="status" hidden></p>
<script>(()=>{const q=id=>document.getElementById(id),msg=q('message'),buttons=document.querySelectorAll('[data-mode]');for(let i=0;i<8;i++)q('m'+i).oninput=()=>q('o'+i).textContent=q('m'+i).value+'%';const show=(ok,text)=>{msg.hidden=false;msg.className='notice message '+(ok?'ok':'err');msg.textContent=text};const markMode=mode=>{q('currentMode').textContent=mode;q('currentMode').className='tag '+(mode==='OFF'?'err':mode==='NORMAL'?'ok':'warn');buttons.forEach(b=>b.classList.toggle('active',b.dataset.mode===mode));q('manual').classList.toggle('active',mode==='MANUAL')};async function status(setLevels){try{const r=await fetch('/api/lumasense/status',{cache:'no-store'});if(!r.ok)return;const s=await r.json();q('profile').value=s.activeProfile;markMode(s.mode);if(setLevels)s.finalLevels.forEach((v,i)=>{q('m'+i).value=v;q('o'+i).textContent=v.toFixed(1)+'%'})}catch(_){}}async function post(url,data){try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}),body=await r.json();show(r.ok,r.ok?'OK: '+body.result:'Blad '+r.status+': '+body.error);if(r.ok)await status(false);return r.ok}catch(_){show(false,'Brak polaczenia');return false}}buttons.forEach(b=>b.onclick=()=>post('/api/lumasense/mode',{mode:b.dataset.mode}));q('profile').onchange=()=>post('/api/lumasense/profile',{profile:Number(q('profile').value)});q('manual').onclick=()=>post('/api/lumasense/manual',{levels:Array.from({length:8},(_,i)=>Number(q('m'+i).value)),timeoutMinutes:Number(q('timeout').value)});q('exitManual').onclick=()=>post('/api/lumasense/mode',{mode:'EXIT_MANUAL'});status(true)})();</script>
)HTML");
}

const char* DiagnosticsPage::route() const {
    return "/diagnostics";
}

const char* DiagnosticsPage::title() const {
    return "Diagnostyka";
}

void DiagnosticsPage::render(
    AquaCore::Web::WebResponseWriter& response
) const {
    response.writeText(R"HTML(
<div class="grid">
<section class="card"><h2>System</h2><div class="row"><span class="key">Og&oacute;lny health</span><span class="value"><span class="tag" id="overall">&#321;adowanie...</span></span></div><div class="row"><span class="key">System health</span><span class="value"><span class="tag" id="sysHealth">-</span></span></div><div class="row"><span class="key">Gotowy</span><strong class="value" id="sysReady">-</strong></div></section>
<section class="card"><h2>Czas</h2><div class="row"><span class="key">Health</span><span class="value"><span class="tag" id="timeHealth">-</span></span></div><div class="row"><span class="key">Stan</span><strong class="value" id="timeState">-</strong></div><div class="row"><span class="key">RTC gotowy</span><strong class="value" id="rtcReady">-</strong></div><div class="row"><span class="key">RTC poprawny</span><strong class="value" id="rtcValid">-</strong></div><div class="row"><span class="key">&#377;r&oacute;d&#322;o</span><strong class="value" id="provider">-</strong></div></section>
<section class="card"><h2>Storage</h2><div class="row"><span class="key">Health</span><span class="value"><span class="tag" id="storageHealth">-</span></span></div><div class="row"><span class="key">Backend gotowy</span><strong class="value" id="backendReady">-</strong></div><div class="row"><span class="key">Poprawny rekord</span><strong class="value" id="validPayload">-</strong></div><div class="row"><span class="key">Aktywny slot</span><strong class="value" id="slot">-</strong></div><div class="row"><span class="key">Generacja</span><strong class="value" id="generation">-</strong></div><div class="row"><span class="key">Ostatni odczyt</span><strong class="value" id="lastLoad">-</strong></div><div class="row"><span class="key">Ostatni zapis</span><strong class="value" id="lastSave">-</strong></div></section>
<section class="card"><h2>Sie&#263;</h2><div class="row"><span class="key">Health</span><span class="value"><span class="tag" id="netHealth">-</span></span></div><div class="row"><span class="key">Dost&#281;pna</span><strong class="value" id="available">-</strong></div><div class="row"><span class="key">Stan</span><strong class="value" id="netState">-</strong></div><div class="row"><span class="key">Po&#322;&#261;czona</span><strong class="value" id="connected">-</strong></div><div class="row"><span class="key">SSID</span><strong class="value" id="ssid">-</strong></div><div class="row"><span class="key">Hostname</span><strong class="value" id="hostname">-</strong></div><div class="row"><span class="key">IP</span><strong class="value" id="netIp">-</strong></div><div class="row"><span class="key">RSSI</span><strong class="value" id="netRssi">-</strong></div><div class="row"><span class="key">Reconnect</span><strong class="value" id="reconnects">-</strong></div><div class="row"><span class="key">AP aktywny</span><strong class="value" id="apActive">-</strong></div><div class="row"><span class="key">AP SSID</span><strong class="value" id="apSsid">-</strong></div><div class="row"><span class="key">AP IP</span><strong class="value" id="apIp">-</strong></div></section>
</div>
<script>(()=>{const e=id=>document.getElementById(id),set=(id,v)=>e(id).textContent=String(v),yes=v=>v?'tak':'nie',tone=v=>v==='ok'?'ok':v==='error'?'err':v==='warning'?'warn':'',badge=(id,v)=>{set(id,v);e(id).className='tag '+tone(v)};fetch('/api/diagnostics',{cache:'no-store'}).then(r=>{if(!r.ok)throw 0;return r.json()}).then(v=>{badge('overall',v.health);badge('sysHealth',v.system.health);set('sysReady',yes(v.system.ready));badge('timeHealth',v.time.health);set('timeState',v.time.state);set('rtcReady',yes(v.time.rtcReady));set('rtcValid',yes(v.time.rtcValid));set('provider',v.time.provider);badge('storageHealth',v.storage.health);set('backendReady',yes(v.storage.backendReady));set('validPayload',yes(v.storage.hasValidPayload));set('slot',v.storage.activeSlot);set('generation',v.storage.generation);set('lastLoad',v.storage.lastLoad);set('lastSave',v.storage.lastSave);badge('netHealth',v.network.health);set('available',yes(v.network.available));set('netState',v.network.state);set('connected',yes(v.network.connected));set('ssid',v.network.ssid||'-');set('hostname',v.network.hostname||'-');set('netIp',v.network.ipAddress);set('netRssi',v.network.rssi+' dBm');set('reconnects',v.network.reconnectCount);set('apActive',yes(v.network.apActive));set('apSsid',v.network.apSsid||'-');set('apIp',v.network.apIpAddress)}).catch(()=>{set('overall','Brak danych');e('overall').className='tag err'})})();</script>
)HTML");
}

const char* SystemPage::route() const {
    return "/system";
}

const char* SystemPage::title() const {
    return "System";
}

void SystemPage::render(
    AquaCore::Web::WebResponseWriter& response
) const {
    response.writeText(R"HTML(
<section class="card"><h2>Informacje systemowe</h2>
<div class="row"><span class="key">Nazwa</span><strong class="value" id="deviceName">&#321;adowanie...</strong></div>
<div class="row"><span class="key">Typ</span><strong class="value" id="deviceType">-</strong></div>
<div class="row"><span class="key">Wariant sprz&#281;tu</span><strong class="value" id="hardware">-</strong></div>
<div class="row"><span class="key">Firmware</span><strong class="value" id="firmware">-</strong></div>
<div class="row"><span class="key">Aqua Core</span><strong class="value" id="aquaCore">-</strong></div>
<div class="row"><span class="key">Czas dzia&#322;ania</span><strong class="value" id="uptime">-</strong></div>
<div class="row"><span class="key">Pow&oacute;d restartu</span><strong class="value" id="restart">-</strong></div>
</section>
<script>(()=>{const e=id=>document.getElementById(id),set=(id,v)=>e(id).textContent=String(v);fetch('/api/system',{cache:'no-store'}).then(r=>{if(!r.ok)throw 0;return r.json()}).then(v=>{set('deviceName',v.deviceName);set('deviceType',v.deviceType);set('hardware',v.hardwareVariant);set('firmware',v.firmwareVersion);set('aquaCore',v.aquaCoreVersion);set('uptime',(v.uptimeMs/1000).toFixed(0)+' s');set('restart',v.restartReason)}).catch(()=>set('deviceName','Brak danych'))})();</script>
)HTML");
}

} // namespace Web
} // namespace LumaSense
