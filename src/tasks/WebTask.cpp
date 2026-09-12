// ============================================================================
//  WebTask.cpp — browser access to the watch: tabbed control panel.
//
//    GET  /                 embedded dark page: 7 tabs (status/display/sound/
//                          wifi/ai/time/files), per-section save buttons
//    GET  /values           JSON snapshot of all non-secret settings (+tz list)
//    GET  /status           JSON telemetry (battery, net, heap, steps, time, sd)
//    GET  /files            JSON listing of /pictures and /music on the SD card
//    GET  /scan[?start=1]   Wi-Fi scan state machine (async driver scan)
//    POST /settings         apply parameters (empty secrets keep current)
//    POST /upload?dir=...   multipart upload (BMP -> /pictures, WAV -> /music)
//    POST /delete           remove one file from /pictures or /music
//    POST /reboot           restart the watch (response flushes first)
//
//  Telemetry served by /status is cached from the EventBus (no I2C inside an
//  HTTP handler).  Settings changes are written to NVS (internal-stack task —
//  allowed) and broadcast so every task applies them live.
// ============================================================================
#include "tasks/WebTask.hpp"
#include "config/config.hpp"
#include "core/Event.hpp"
#include "core/Logger.hpp"
#include "core/PwrState.hpp"
#include "core/Settings.hpp"
#include <Arduino.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

namespace tasks {

namespace {
    constexpr const char* kTag = "Web";

    WebServer* server = nullptr;
    tasks::WebTask* g_webtask = nullptr;
    File upload_file;
    bool upload_ok = false;
    uint32_t reboot_at_ms = 0;

    // Telemetry cache fed by the EventBus — HTTP handlers only read it.
    struct {
        uint8_t  percent = 0;
        bool     charging = false;
        uint16_t voltage_mv = 0;
    } st_bat;
    uint32_t st_steps = 0;
    bool     st_conn = false;
    int8_t   st_rssi = 0;

    String jsonEscape(const char* s) {
        String out;
        for (const char* p = s ? s : ""; *p; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
            else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04X", c); out += b; }
            else out += static_cast<char>(c);
        }
        return out;
    }

    const char PAGE[] PROGMEM = R"html(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Часы — управление</title><style>
:root{--bg:#0d1116;--cd:#141a21;--in:#1b242e;--bd:#2e3b49;--tx:#e6edf3;--mu:#8fa3b5;--ac:#4fc3f7;--bt:#1565c0;--ok:#7ed957;--wr:#ffb74d}
*{box-sizing:border-box}body{background:var(--bg);color:var(--tx);font-family:system-ui,sans-serif;max-width:560px;margin:0 auto;padding:12px 12px 60px}
h2{margin:8px 0 12px;font-size:22px}h3{color:var(--ac);margin:0 0 10px;font-size:16px}
#tabs{display:flex;gap:6px;overflow-x:auto;padding-bottom:6px;margin-bottom:6px;scrollbar-width:none}
#tabs::-webkit-scrollbar{display:none;height:0}
#tabs button{flex:0 0 auto;background:var(--cd);color:var(--mu);border:1px solid var(--bd);border-radius:10px;padding:8px 13px;cursor:pointer;font-size:14px}
#tabs button.on{background:var(--bt);color:#fff;border-color:var(--bt)}
.card{background:var(--cd);border-radius:14px;padding:14px;margin:10px 0}
label{display:block;color:var(--mu);margin:10px 0 4px;font-size:14px}
input,select{width:100%;background:var(--in);color:var(--tx);border:1px solid var(--bd);border-radius:8px;padding:8px;font-size:15px}
input[type=range]{padding:0;accent-color:var(--bt)}
button.act{background:var(--bt);color:#fff;border:0;border-radius:10px;padding:9px 18px;cursor:pointer;font-size:15px;margin-top:10px}
button.act:active{opacity:.8}button.act:disabled{opacity:.5}
.hint{color:var(--mu);font-size:12px;margin:4px 0 2px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.kv{background:var(--cd);border-radius:12px;padding:10px;min-width:0}
.kv b{display:block;font-size:17px;font-weight:600;word-break:break-all}
.kv span{color:var(--mu);font-size:12px}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:#15271a;color:var(--ok);border:1px solid var(--ok);border-radius:10px;padding:8px 16px;display:none;font-size:14px;max-width:90%}
#toast.err{background:#2d1517;color:#ff8a80;border-color:#ff8a80}
.bar{height:8px;background:var(--in);border-radius:4px;overflow:hidden;margin-top:8px}
.bar i{display:block;height:100%;width:0;background:var(--ac);transition:width .2s}
.net{padding:9px 10px;border-radius:8px;background:var(--in);margin:6px 0;cursor:pointer;display:flex;justify-content:space-between;gap:8px}
.net:hover{outline:1px solid var(--ac)}
.net .s{color:var(--mu);white-space:nowrap}
.file{display:flex;align-items:center;gap:8px;padding:8px 10px;border-radius:8px;background:var(--in);margin:6px 0}
.file .n{flex:1;word-break:break-all}.file .s{color:var(--mu);font-size:12px;white-space:nowrap}
button.del{background:#4a2020;color:#ff8a80;border:0;border-radius:8px;padding:6px 10px;cursor:pointer}
.warn{background:#2b2413;color:var(--wr);border-radius:10px;padding:10px;font-size:13px;margin:10px 0}
.uprow{display:flex;gap:8px;align-items:center}.uprow input{flex:1}
</style></head><body>
<h2>&#9203; Часы — управление</h2>
<nav id=tabs>
<button data-t=status>Статус</button><button data-t=display>Дисплей</button>
<button data-t=sound>Звук</button><button data-t=wifi>Wi-Fi</button>
<button data-t=ai>ИИ</button><button data-t=time>Время</button>
<button data-t=files>Файлы</button></nav>

<section id=t-status>
<div class=grid>
<div class=kv><span>Батарея</span><b id=s_bat>—</b><span id=s_vbat></span></div>
<div class=kv><span>Wi-Fi</span><b id=s_wifi>—</b><span id=s_rssi></span></div>
<div class=kv><span>IP-адрес</span><b id=s_ip>—</b></div>
<div class=kv><span>Шаги</span><b id=s_steps>—</b></div>
<div class=kv><span>Свободная память</span><b id=s_heap>—</b><span id=s_psram></span></div>
<div class=kv><span>Аптайм</span><b id=s_up>—</b></div>
<div class=kv><span>Время устройства</span><b id=s_time>—</b></div>
<div class=kv><span>SD-карта</span><b id=s_sd>—</b></div>
<div class=kv><span>Прошивка</span><b id=s_fw>—</b></div>
<div class=kv><span>MAC</span><b id=s_mac>—</b></div>
</div>
<div id=apbox class=warn hidden></div>
<p><button class=act id=reboot>&#8635; Перезагрузить часы</button></p>
<p class=hint>Статус обновляется каждые 5 секунд.</p>
</section>

<section id=t-display hidden>
<div class=card><h3>Дисплей</h3>
<label>Яркость подсветки: <b id=bv></b>%</label>
<input type=range id=bright min=10 max=255>
<p class=hint>Применяется сразу после отпускания ползунка.</p>
<label>Таймаут отключения подсветки</label>
<select id=dim><option value=15>15 секунд</option><option value=30>30 секунд</option>
<option value=60>1 минута</option><option value=300>5 минут</option>
<option value=0>Не выключать</option></select>
<p><button class=act data-f="dim">Сохранить</button></p>
</div>
</section>

<section id=t-sound hidden>
<div class=card><h3>Звук</h3>
<label>Громкость динамика: <b id=vv></b>%</label>
<input type=range id=vol min=0 max=100>
<label>Усиление микрофона: <b id=mg></b></label>
<input type=range id=micg min=0 max=8>
<p class=hint>Применяется сразу после отпускания ползунка (0&ndash;8 = 0&ndash;48 дБ, шаг 6 дБ).</p>
</div>
</section>

<section id=t-wifi hidden>
<div class=card><h3>Подключение</h3>
<p class=hint id=w_state>—</p></div>
<div class=card><h3>Поиск сетей</h3>
<p><button class=act id=scanbtn>Сканировать</button></p>
<div id=scanbox><p class=hint>Нажмите «Сканировать», затем коснитесь сети, чтобы подставить её SSID.</p></div>
</div>
<div class=card><h3>Настройки сети</h3>
<label>Wi-Fi сеть (SSID)</label><input id=ssid autocomplete=off>
<label>Wi-Fi пароль (пусто — не менять)</label><input id=pass type=password>
<p><button class=act data-f="ssid,pass" data-m="Сохранено — часы переподключаются">Сохранить и подключить</button></p>
<p class=hint>При сохранении часы переподключатся к новой сети; страница может стать недоступной, если IP изменится. Пустой SSID сбрасывает Wi-Fi (часы поднимут точку доступа Watch-XXXX).</p>
</div>
<div class=card id=apcard hidden><h3>Точка доступа</h3><p class=hint id=apinfo></p></div>
</section>

<section id=t-ai hidden>
<div class=card><h3>ИИ-ассистент</h3>
<label>Бэкенд</label>
<select id=backend><option value=0>XiaoZhi (официальный / совместимый)</option>
<option value=1>OpenAI-совместимый</option></select>
<div id=g_xz>
<label>WebSocket URL</label><input id=wsurl autocomplete=off>
<label>Токен (пусто — не менять)</label><input id=token type=password>
<p class=hint>Официальный сервер требует привязки устройства. Для DIY-сборок используйте совместимый сервер xiaozhi-протокола (например, xiaozhi-esp32-server) — укажите его URL и токен.</p>
</div>
<div id=g_oa hidden>
<label>API URL (&hellip;/v1/chat/completions)</label><input id=apiurl autocomplete=off>
<label>API-ключ (пусто — не менять)</label><input id=key type=password>
<label>Модель</label><input id=model autocomplete=off>
<p class=hint>Любой OpenAI-совместимый эндпоинт /v1/chat/completions.</p>
</div>
<p><button class=act data-f="backend,wsurl,token,apiurl,key,model">Сохранить и применить</button></p>
</div>
</section>

<section id=t-time hidden>
<div class=card><h3>Время</h3>
<p class=hint>Часы сейчас: <b id=t_now>—</b></p>
<label>Часовой пояс</label><select id=tz></select>
<p><button class=act data-f="tz">Сохранить</button></p>
<p class=hint>Время синхронизируется по NTP при подключении к Wi-Fi.</p>
</div>
</section>

<section id=t-files hidden>
<div class=card><h3>SD-карта</h3><p class=hint id=sdline>—</p>
<div class=bar><i id=sdbar></i></div></div>
<div class=card><h3>Картинки (BMP &rarr; /pictures)</h3>
<p class=hint>24/32-бит BMP, до 410&times;502 (просмотр в приложении «Просмотр»).</p>
<div class=uprow><input type=file id=f1 accept=.bmp><button class=act id=ub1>Загрузить</button></div>
<div class=bar><i id=pb1></i></div><div id=fl1></div></div>
<div class=card><h3>Музыка (WAV &rarr; /music)</h3>
<p class=hint>16-бит PCM, 8&ndash;48 кГц, моно/стерео (плеер на часах).</p>
<div class=uprow><input type=file id=f2 accept=.wav><button class=act id=ub2>Загрузить</button></div>
<div class=bar><i id=pb2></i></div><div id=fl2></div></div>
</section>

<div id=toast></div>
<script>
const $=i=>document.getElementById(i);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function toast(t,err){const o=$('toast');o.textContent=t;o.className=err?'err':'';o.style.display='block';clearTimeout(o._t);o._t=setTimeout(()=>o.style.display='none',2600)}
function post(o){return fetch('/settings',{method:'POST',body:new URLSearchParams(o)}).then(r=>{if(!r.ok)throw 0})}
const fmtB=b=>b>=1048576?(b/1048576).toFixed(1)+' МБ':b>=1024?Math.round(b/1024)+' КБ':b+' Б';
const fmtUp=s=>{const d=Math.floor(s/86400),h=String(Math.floor(s%86400/3600)).padStart(2,'0'),m=String(Math.floor(s%3600/60)).padStart(2,'0'),x=String(s%60).padStart(2,'0');return(d?d+'д ':'')+h+':'+m+':'+x};

// ---- tabs ----
const tabs=document.querySelectorAll('#tabs button');
function show(id){tabs.forEach(b=>b.classList.toggle('on',b.dataset.t==id));
document.querySelectorAll('section').forEach(s=>s.hidden=s.id!='t-'+id);
if(id=='files')loadFiles();if(id=='status')loadStatus();
history.replaceState(null,'','#'+id)}
tabs.forEach(b=>b.onclick=()=>show(b.dataset.t));
show(['status','display','sound','wifi','ai','time','files'].includes(location.hash.slice(1))?location.hash.slice(1):'status');

// ---- live sliders (single POST per release) ----
$('bright').oninput=e=>$('bv').textContent=Math.round(e.target.value/255*100);
$('bright').onchange=e=>post({bright:e.target.value}).then(()=>toast('Яркость: '+Math.round(e.target.value/255*100)+'%')).catch(()=>toast('Ошибка',1));
$('vol').oninput=e=>$('vv').textContent=e.target.value;
$('vol').onchange=e=>post({vol:e.target.value}).then(()=>toast('Громкость: '+e.target.value+'%')).catch(()=>toast('Ошибка',1));
$('micg').oninput=e=>$('mg').textContent=e.target.value*6+' дБ';
$('micg').onchange=e=>post({micg:e.target.value}).then(()=>toast('Микрофон: '+e.target.value*6+' дБ')).catch(()=>toast('Ошибка',1));

// ---- per-section save buttons (empty secrets are omitted) ----
document.querySelectorAll('button[data-f]').forEach(b=>b.onclick=()=>{
const o={};b.dataset.f.split(',').forEach(k=>{const v=$(k).value;
if((k=='pass'||k=='token'||k=='key')&&!v)return;o[k]=v});
post(o).then(()=>toast(b.dataset.m||'Сохранено и применено')).catch(()=>toast('Ошибка сохранения',1))});

// ---- backend field groups ----
function syncBk(){const oa=+$('backend').value==1;$('g_xz').hidden=oa;$('g_oa').hidden=!oa}
$('backend').onchange=syncBk;

// ---- initial form values ----
fetch('/values').then(r=>r.json()).then(j=>{
$('bright').value=j.bright;$('bright').dispatchEvent(new Event('input'));
$('vol').value=j.vol;$('vol').dispatchEvent(new Event('input'));
$('micg').value=j.micg;$('micg').dispatchEvent(new Event('input'));
$('dim').value=String(j.dim);$('backend').value=String(j.backend);syncBk();
$('ssid').value=j.ssid;$('wsurl').value=j.wsurl;$('apiurl').value=j.apiurl;$('model').value=j.model;
$('tz').innerHTML=(j.tzNames||[]).map((n,i)=>`<option value=${i}>${esc(n)}</option>`).join('');
$('tz').value=String(j.tz);}).catch(()=>toast('Нет связи с часами',1));

// ---- status poll ----
function loadStatus(){fetch('/status').then(r=>r.json()).then(j=>{
$('s_bat').textContent=j.bat+'%'+(j.chg?' ⚡ заряжается':'');
$('s_vbat').textContent=(j.vbat/1000).toFixed(2)+' В';
$('s_wifi').textContent=j.conn?j.ssid:'нет подключения';
$('s_rssi').textContent=j.conn?j.rssi+' dBm':'';
$('s_ip').textContent=j.ip||'—';$('s_steps').textContent=j.steps;
$('s_heap').textContent=j.heap+' КБ (мин '+j.hmin+')';
$('s_psram').textContent='PSRAM: '+j.psram+' КБ';
$('s_up').textContent=fmtUp(j.up);$('s_time').textContent=j.time;$('t_now').textContent=j.time;
$('s_sd').textContent=j.sd.ok?fmtB(j.sd.used*1048576)+' / '+fmtB(j.sd.total*1048576):'нет карты';
$('s_fw').textContent='v'+j.fw;$('s_mac').textContent=j.mac;
$('w_state').textContent=j.conn?('Подключено: «'+j.ssid+'», IP '+j.ip+', '+j.rssi+' dBm'):'Не подключено — часы в оффлайне или раздают точку доступа';
$('apbox').hidden=!j.ap.on;$('apcard').hidden=!j.ap.on;
if(j.ap.on){$('apinfo').textContent='Точка доступа: «'+j.ap.ssid+'», пароль 12345678, адрес http://'+j.ap.ip;
$('apbox').textContent='Часы раздают Wi-Fi «'+j.ap.ssid+'» (пароль 12345678). Панель: http://'+j.ap.ip;}
}).catch(()=>{})}
setInterval(()=>{if(!$('t-status').hidden||!$('t-wifi').hidden||!$('t-time').hidden)loadStatus()},5000);
loadStatus();

// ---- Wi-Fi scan (async on the device, polled here) ----
let scanT=null;
function pollScan(){fetch('/scan').then(r=>r.json()).then(j=>{
if(j.state=='run'){$('scanbox').innerHTML='<p class=hint>Сканирование…</p>';scanT=setTimeout(pollScan,700);return}
if(j.state!='done'){$('scanbox').innerHTML='<p class=hint>Сканирование не удалось.</p>';return}
const seen={};const nets=(j.nets||[]).filter(n=>n.s&&!seen[n.s]&&(seen[n.s]=1)).sort((a,b)=>b.r-a.r);
$('scanbox').innerHTML=nets.length?nets.map(n=>`<div class=net data-s="${esc(n.s)}"><span>${esc(n.s)}</span><span class=s>${n.r} dBm ${n.sec?'&#128274;':''}</span></div>`).join(''):'<p class=hint>Сети не найдены.</p>';
$('scanbox').querySelectorAll('.net').forEach(el=>el.onclick=()=>{$('ssid').value=el.dataset.s;toast('Сеть подставлена: '+el.dataset.s)})}).catch(()=>{$('scanbox').innerHTML='<p class=hint>Ошибка запроса.</p>'})}
$('scanbtn').onclick=()=>{clearTimeout(scanT);$('scanbox').innerHTML='<p class=hint>Сканирование…</p>';
fetch('/scan?start=1').then(()=>{scanT=setTimeout(pollScan,900)})};

// ---- files ----
function drawList(id,arr,dir){const ul=$(id);ul.innerHTML='';
(arr||[]).forEach(f=>{const d=document.createElement('div');d.className='file';
const n=document.createElement('span');n.className='n';n.textContent=f.n;
const s=document.createElement('span');s.className='s';s.textContent=fmtB(f.s);
const b=document.createElement('button');b.className='del';b.textContent='✕';
b.onclick=()=>{if(!confirm('Удалить '+f.n+'?'))return;
fetch('/delete',{method:'POST',body:new URLSearchParams({path:'/'+dir+'/'+f.n})})
.then(r=>{if(!r.ok)throw 0;toast('Удалено: '+f.n);loadFiles()}).catch(()=>toast('Ошибка удаления',1))};
d.append(n,s,b);ul.append(d)});
if(!ul.children.length)ul.innerHTML='<p class=hint>Папка пуста.</p>'}
function loadFiles(){fetch('/files').then(r=>r.json()).then(j=>{
$('sdline').textContent=j.sd.ok?fmtB(j.sd.used*1048576)+' из '+fmtB(j.sd.total*1048576):'Карта не найдена';
$('sdbar').style.width=j.sd.ok&&j.sd.total?(j.sd.used/j.sd.total*100)+'%':'0';
drawList('fl1',j.pictures,'pictures');drawList('fl2',j.music,'music')}).catch(()=>toast('Ошибка чтения SD',1))}
function upload(dir,inp,bar){const f=inp.files[0];if(!f)return toast('Выберите файл',1);
const x=new XMLHttpRequest();x.open('POST','/upload?dir='+dir);
$('ub1').disabled=$('ub2').disabled=true;
x.upload.onprogress=e=>{$(bar).style.width=(e.loaded/e.total*100)+'%'};
x.onloadend=()=>{$(bar).style.width='0';$('ub1').disabled=$('ub2').disabled=false;inp.value='';
if(x.status==200&&x.responseText.indexOf('ERR')<0){toast('Загружено: '+f.name);loadFiles()}else toast('Ошибка загрузки',1)};
x.onerror=()=>{$(bar).style.width='0';$('ub1').disabled=$('ub2').disabled=false;toast('Ошибка сети',1)};
const fd=new FormData();fd.append('f',f);x.send(fd)}
$('ub1').onclick=()=>upload('pictures',$('f1'),'pb1');
$('ub2').onclick=()=>upload('music',$('f2'),'pb2');

// ---- reboot ----
$('reboot').onclick=()=>{if(!confirm('Перезагрузить часы?'))return;
toast('Перезагрузка…');fetch('/reboot',{method:'POST'}).catch(()=>{})};
</script></body></html>)html";
}

// ----------------------------------------------------------------------------
//  JSON helpers
// ----------------------------------------------------------------------------

// [{"n":"name","s":123}, …] — File::name() has no path prefix, names only.
String dirListingJson(const char* dir) {
    String out = "[";
    File root = SD_MMC.open(dir);
    if (root && root.isDirectory()) {
        File f;
        bool first = true;
        while ((f = root.openNextFile())) {
            if (!f.isDirectory()) {
                if (!first) out += ",";
                first = false;
                out += String("{\"n\":\"") + jsonEscape(f.name()) +
                       "\",\"s\":" + String(static_cast<unsigned>(f.size())) + "}";
            }
            f.close();
        }
    }
    if (root) root.close();
    out += "]";
    return out;
}

// ----------------------------------------------------------------------------
//  Handlers
// ----------------------------------------------------------------------------

// Every handler marks activity: NetTask reads this to hold the Wi-Fi radio
// awake while somebody actually uses the panel.
void markWeb() {
    core::pwr::lastWebMs.store(millis());
}

void handleRoot() {
    markWeb();
    server->send_P(200, "text/html", PAGE);
}

void handleValues() {
    markWeb();
    const auto& s = core::Settings::instance().d();
    String j = String("{\"bright\":") + s.brightness +
               ",\"vol\":" + s.volume +
               ",\"micg\":" + s.micGain +
               ",\"dim\":" + s.dimTimeoutS +
               ",\"tz\":" + s.tzIndex +
               ",\"backend\":" + s.aiBackend +
               ",\"ssid\":\"" + jsonEscape(s.wifiSsid) + "\"" +
               ",\"wsurl\":\"" + jsonEscape(s.aiWsUrl) + "\"" +
               ",\"apiurl\":\"" + jsonEscape(s.aiApiUrl) + "\"" +
               ",\"model\":\"" + jsonEscape(s.aiModel) + "\"";
    size_t n = 0;
    const core::TzPreset* tz = core::tzPresets(n);
    j += ",\"tzNames\":[";
    for (size_t i = 0; i < n; ++i) {
        if (i) j += ",";
        j += String("\"") + jsonEscape(tz[i].label) + "\"";
    }
    j += "]}";
    server->send(200, "application/json", j);
}

void handleStatus() {
    markWeb();
    char tbuf[24] = "—";
    time_t now = time(nullptr);
    if (now > 100000) {  // clock set (SNTP/RTC), not 1970
        tm lt;
        localtime_r(&now, &lt);
        strftime(tbuf, sizeof tbuf, "%H:%M:%S %d.%m.%Y", &lt);
    }

    const bool ap_on = (WiFi.getMode() & WIFI_MODE_AP) != 0;
    const bool sd_ok = SD_MMC.cardType() != CARD_NONE;
    String j = String("{\"fw\":\"") + cfg::kFwVersion + "\"" +
               ",\"mac\":\"" + WiFi.macAddress() + "\"" +
               ",\"ip\":\"" + (st_conn ? WiFi.localIP().toString()
                                       : WiFi.softAPIP().toString()) + "\"" +
               ",\"ssid\":\"" + jsonEscape(WiFi.SSID().c_str()) + "\"" +
               ",\"rssi\":" + String(st_rssi) +
               ",\"conn\":" + (st_conn ? "true" : "false") +
               ",\"ap\":{\"on\":" + (ap_on ? "true" : "false") +
               ",\"ssid\":\"" + jsonEscape(WiFi.softAPSSID().c_str()) + "\"" +
               ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"}" +
               ",\"bat\":" + String(st_bat.percent) +
               ",\"chg\":" + (st_bat.charging ? "true" : "false") +
               ",\"vbat\":" + String(st_bat.voltage_mv) +
               ",\"heap\":" + String(ESP.getFreeHeap() / 1024) +
               ",\"hmin\":" + String(ESP.getMinFreeHeap() / 1024) +
               ",\"psram\":" + String(ESP.getFreePsram() / 1024) +
               ",\"up\":" + String(millis() / 1000) +
               ",\"steps\":" + String(st_steps) +
               ",\"time\":\"" + tbuf + "\"" +
               ",\"sd\":{\"ok\":" + (sd_ok ? "true" : "false") +
               ",\"used\":" + String(sd_ok ? (uint32_t)(SD_MMC.usedBytes() / (1024ULL * 1024)) : 0) +
               ",\"total\":" + String(sd_ok ? (uint32_t)(SD_MMC.totalBytes() / (1024ULL * 1024)) : 0) +
               "}}";
    server->send(200, "application/json", j);
}

void handleFiles() {
    markWeb();
    const bool sd_ok = SD_MMC.cardType() != CARD_NONE;
    String j = String("{\"pictures\":") + dirListingJson("/pictures") +
               ",\"music\":" + dirListingJson("/music") +
               ",\"sd\":{\"ok\":" + (sd_ok ? "true" : "false") +
               ",\"used\":" + String(sd_ok ? (uint32_t)(SD_MMC.usedBytes() / (1024ULL * 1024)) : 0) +
               ",\"total\":" + String(sd_ok ? (uint32_t)(SD_MMC.totalBytes() / (1024ULL * 1024)) : 0) +
               "}}";
    server->send(200, "application/json", j);
}

void handleDelete() {
    markWeb();
    if (!server->hasArg("path")) {
        server->send(400, "text/plain", "no path");
        return;
    }
    String p = server->arg("path");
    // Only white-listed media folders, no traversal, no bare folder paths.
    if ((!p.startsWith("/pictures/") && !p.startsWith("/music/")) ||
        p.indexOf("..") >= 0 || p.length() < 10) {
        server->send(400, "text/plain", "bad path");
        return;
    }
    if (!SD_MMC.exists(p) || !SD_MMC.remove(p)) {
        server->send(500, "text/plain", "remove failed");
        return;
    }
    LOGI(kTag, "deleted %s", p.c_str());
    server->send(200, "text/plain", "OK");
}

void handleScan() {
    markWeb();
    if (server->hasArg("start") &&
        WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        WiFi.scanNetworks(true);  // async: the WiFi task does the sweeping
    }
    String j = "{\"state\":\"";
    const int st = WiFi.scanComplete();
    if (st == WIFI_SCAN_RUNNING) {
        j += "run\",\"nets\":[]}";
    } else if (st >= 0) {
        j += "done\",\"nets\":[";
        for (int i = 0; i < st; ++i) {
            if (i) j += ",";
            j += String("{\"s\":\"") + jsonEscape(WiFi.SSID(i).c_str()) +
                 "\",\"r\":" + String(WiFi.RSSI(i)) +
                 ",\"sec\":" +
                 (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "0" : "1") + "}";
        }
        j += "]}";
        WiFi.scanDelete();  // free the driver result buffer
    } else {
        j += "fail\",\"nets\":[]}";
    }
    server->send(200, "application/json", j);
}

void handleReboot() {
    markWeb();
    server->send(200, "text/plain", "OK");
    reboot_at_ms = millis() + 800;  // let the response flush, then restart
}

void handleSettings() {
    markWeb();
    auto& s = core::Settings::instance().d();
    uint8_t mask = 0;

    if (server->hasArg("bright")) {
        int v = server->arg("bright").toInt();
        s.brightness = static_cast<uint8_t>(v < 10 ? 10 : (v > 255 ? 255 : v));
        mask |= core::kSettingsDisplay;
    }
    if (server->hasArg("dim")) {
        uint16_t vals[] = {15, 30, 60, 300, 0};
        long v = server->arg("dim").toInt();
        for (uint16_t x : vals) {
            if (x == v) {
                s.dimTimeoutS = static_cast<uint16_t>(v);
                mask |= core::kSettingsDisplay;
                break;
            }
        }
    }
    if (server->hasArg("vol")) {
        long v = server->arg("vol").toInt();
        s.volume = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
        mask |= core::kSettingsSound;
    }
    if (server->hasArg("micg")) {
        long v = server->arg("micg").toInt();
        s.micGain = static_cast<uint8_t>(v < 0 ? 0 : (v > 8 ? 8 : v));
        mask |= core::kSettingsSound;
    }
    if (server->hasArg("tz")) {
        long v = server->arg("tz").toInt();
        size_t n = 0;
        core::tzPresets(n);
        if (v >= 0 && v < static_cast<long>(n)) {
            s.tzIndex = static_cast<uint8_t>(v);
            mask |= core::kSettingsTimezone;
        }
    }
    if (server->hasArg("ssid")) {
        strlcpy(s.wifiSsid, server->arg("ssid").c_str(), sizeof(s.wifiSsid));
        mask |= core::kSettingsWifi;
    }
    if (server->hasArg("pass") && server->arg("pass").length()) {
        strlcpy(s.wifiPass, server->arg("pass").c_str(), sizeof(s.wifiPass));
        mask |= core::kSettingsWifi;
    }
    if (server->hasArg("backend")) {
        s.aiBackend = server->arg("backend") == "1" ? 1 : 0;
        mask |= core::kSettingsAi;
    }
    if (server->hasArg("wsurl")) {
        strlcpy(s.aiWsUrl, server->arg("wsurl").c_str(), sizeof(s.aiWsUrl));
        mask |= core::kSettingsAi;
    }
    if (server->hasArg("token") && server->arg("token").length()) {
        strlcpy(s.aiToken, server->arg("token").c_str(), sizeof(s.aiToken));
        mask |= core::kSettingsAi;
    }
    if (server->hasArg("apiurl")) {
        strlcpy(s.aiApiUrl, server->arg("apiurl").c_str(), sizeof(s.aiApiUrl));
        mask |= core::kSettingsAi;
    }
    if (server->hasArg("key") && server->arg("key").length()) {
        strlcpy(s.aiApiKey, server->arg("key").c_str(), sizeof(s.aiApiKey));
        mask |= core::kSettingsAi;
    }
    if (server->hasArg("model")) {
        strlcpy(s.aiModel, server->arg("model").c_str(), sizeof(s.aiModel));
        mask |= core::kSettingsAi;
    }

    if (mask) {
        core::Settings::instance().save();  // internal-stack task: NVS is safe
        core::EventBus::instance().publish(
            core::Event::makeSettingsChanged(mask));
        LOGI(kTag, "settings applied from web (mask=0x%02X)", mask);
        server->send(200, "text/plain", "OK");
    } else {
        server->send(400, "text/plain", "no parameters");
    }
}

void handleUpload() {
    markWeb();
    HTTPUpload& up = server->upload();
    if (up.status == UPLOAD_FILE_START) {
        upload_ok = false;
        String name = up.filename;
        int slash = name.lastIndexOf('/');
        int bs = name.lastIndexOf('\\');
        name = name.substring((slash > bs ? slash : bs) + 1);
        String lower = name; lower.toLowerCase();

        String dir;
        if (lower.endsWith(".bmp")) dir = "/pictures";
        else if (lower.endsWith(".wav")) dir = "/music";
        else { LOGW(kTag, "rejected upload type: %s", name.c_str()); return; }

        if (!SD_MMC.exists(dir)) SD_MMC.mkdir(dir);
        String path = dir + "/" + name;
        if (upload_file) upload_file.close();
        upload_file = SD_MMC.open(path, FILE_WRITE);
        upload_ok = (bool)upload_file;
        LOGI(kTag, "upload start: %s", path.c_str());
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (upload_file) upload_file.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
        if (upload_file) {
            LOGI(kTag, "upload done: %s (%u B)", upload_file.path(),
                 static_cast<unsigned>(upload_file.size()));
            upload_file.close();
        }
    }
}

}  // namespace tasks

void tasks::WebTask::run() {
    g_webtask = this;

    // Seed the telemetry cache before the first HTTP request can arrive.
    const auto b0 = hal_.power.readBattery();
    st_bat.percent = b0.percent;
    st_bat.charging = b0.charging;
    st_bat.voltage_mv = b0.voltage_mv;
    core::EventBus::instance().subscribe(mailbox());

    server = new WebServer(80);
    server->on("/", HTTP_GET, handleRoot);
    server->on("/values", HTTP_GET, handleValues);
    server->on("/status", HTTP_GET, handleStatus);
    server->on("/files", HTTP_GET, handleFiles);
    server->on("/scan", HTTP_GET, handleScan);
    server->on("/settings", HTTP_POST, handleSettings);
    server->on("/reboot", HTTP_POST, handleReboot);
    server->on("/delete", HTTP_POST, handleDelete);
    server->on("/upload", HTTP_POST,
               []() { server->send(upload_ok ? 200 : 400, "text/plain",
                                   upload_ok ? "OK" : "ERR: .bmp/.wav only"); },
               handleUpload);
    server->onNotFound([]() { server->send(404, "text/plain", "404"); });
    server->begin();
    LOGI(kTag, "HTTP server ready on port 80");

#ifdef WEB_SELF_TEST
    // temporary diagnostics: fetch our own pages from a detached task so the
    // server loop keeps servicing while the client waits
    class SelfTest {
    public:
        static void trampoline(void*) {
            vTaskDelay(pdMS_TO_TICKS(8000));
            const char* paths[] = {"/", "/values", "/status", "/files"};
            for (const char* p : paths) {
                HTTPClient http;
                http.begin(String("http://127.0.0.1") + p);
                http.setTimeout(15000);
                const int code = http.GET();
                const int len = (code == 200) ? http.getSize() : -1;
                String body = (code == 200) ? http.getString() : "";
                http.end();
                LOGI(kTag, "SELF-TEST %s -> %d len=%d", p, code, len);
                LOGI(kTag, "SELF-TEST body: %s",
                     body.substring(0, 120).c_str());
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            vTaskDelete(nullptr);
        }
    };
    xTaskCreatePinnedToCore(SelfTest::trampoline, "wsTest", 8192, nullptr, 1,
                            nullptr, 0);
#endif

    uint8_t applied_vol = 0xFF, applied_micg = 0xFF;
    while (true) {
        // Refresh the telemetry cache from the bus (non-blocking).
        core::Event e;
        while (waitFor(e, 0)) {
            switch (e.type) {
                case core::EventType::Battery:
                    st_bat.percent = e.battery.percent;
                    st_bat.charging = e.battery.charging;
                    st_bat.voltage_mv = e.battery.voltage_mv;
                    break;
                case core::EventType::StepCount:
                    st_steps = e.steps.steps;
                    break;
                case core::EventType::NetStatus:
                    st_conn = e.net.connected;
                    st_rssi = e.net.rssi;
                    break;
                default:
                    break;
            }
        }

        if (reboot_at_ms && (int32_t)(millis() - reboot_at_ms) >= 0) {
            LOGI(kTag, "reboot requested from web");
            server->stop();
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP.restart();
        }

        // keep audio in sync with the persisted sound settings
        const auto& sc = core::Settings::instance().d();
        if (applied_vol != sc.volume) {
            applied_vol = sc.volume;
            hal_.audio.setVolume(sc.volume);
        }
        if (applied_micg != sc.micGain) {
            applied_micg = sc.micGain;
            hal_.audio.setMicGain(sc.micGain);
        }
        // serve in both STA and AP modes
        server->handleClient();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
