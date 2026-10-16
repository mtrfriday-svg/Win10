#include "web_control.h"
#include "config.h"
#include "mdee_robot.h"
#include "audio_url_player.h"
#include "favorites.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define TAG "WebControl"

// Optional PIN. Leave "" for open control on your home Wi-Fi, or set e.g. "1234".
#ifndef WEB_CONTROL_PIN
#define WEB_CONTROL_PIN ""
#endif

static const char kPage[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>M'DEE</title><style>
:root{--bg:#0b0f14;--card:#151b23;--fg:#e8eef5;--g:#2ee66b;--m:#7d8a99}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;touch-action:manipulation}
body{margin:0;background:var(--bg);color:var(--fg);font:15px system-ui,sans-serif;padding:12px;max-width:520px;margin:auto}
h1{font-size:18px;margin:4px 0 10px;color:var(--g)} .card{background:var(--card);border-radius:14px;padding:12px;margin-bottom:12px}
.row{display:flex;gap:8px;flex-wrap:wrap} button{flex:1;min-width:70px;padding:12px 6px;border:0;border-radius:10px;background:#222c38;color:var(--fg);font-size:15px}
button.on{background:var(--g);color:#04210f;font-weight:700} button.stop{background:#e5484d;color:#fff;font-weight:700;width:100%;padding:16px}
#joy{width:230px;height:230px;border-radius:50%;background:#10161d;border:2px solid #263241;margin:6px auto;position:relative;touch-action:none}
#nub{width:84px;height:84px;border-radius:50%;background:var(--g);position:absolute;left:73px;top:73px;opacity:.9}
label{display:block;color:var(--m);font-size:13px;margin-top:6px} input[type=range]{width:100%}
#st{color:var(--m);font-size:13px;line-height:1.5}
</style></head><body><h1>M'DEE control</h1>
<div class="card"><div id="st">connecting…</div></div>
<div class="card"><div class="row" id="spd"><button data-s="slow">Slow</button><button data-s="medium" class="on">Medium</button><button data-s="fast">Fast</button><button data-s="turbo">Turbo</button></div>
<div id="joy"><div id="nub"></div></div>
<button class="stop" onclick="stopAll()">STOP</button></div>
<div class="card"><div class="row" id="mode"><button data-m="manual" class="on">Manual</button><button data-m="wander">Wander</button><button data-m="follow">Follow</button></div></div>
<div class="card"><label>Neck turn (left/right)</label><input id="pan" type="range" min="0" max="180" value="90">
<label>Neck tilt (up/down)</label><input id="tilt" type="range" min="0" max="180" value="90">
<div class="row" id="ears" style="margin-top:10px"><button data-e="perk">Ears up</button><button data-e="droop">Ears down</button><button data-e="wiggle">Wiggle</button><button data-e="auto">Auto</button></div></div>
<div class="card"><div class="row" id="lt"><button data-l="off">Off</button><button data-l="on">On</button><button data-l="chase">Chase</button><button data-l="blink">Blink</button><button data-l="battery">Battery</button></div>
<div class="row" style="margin-top:8px"><button data-n="rainbow">Rainbow</button><button data-n="breathe">Breathe</button><button data-c="255,0,0">Red</button><button data-c="0,255,0">Green</button><button data-c="0,80,255">Blue</button></div></div>
<div class="card"><label>Play internet audio (direct MP3 link)</label>
<input id="url" type="url" placeholder="https://.../song.mp3" style="width:100%;padding:10px;border-radius:8px;border:0;background:#10161d;color:#e8eef5;margin:6px 0">
<div class="row"><button id="aplay">Play</button><button id="astop" class="stop" style="width:auto">Stop</button></div>
<div class="row" id="favs" style="margin-top:8px"></div><div id="am" style="color:var(--m);font-size:13px;margin-top:6px"></div></div>
<script>
const K=new URLSearchParams(location.search).get('k')||localStorage.k||'';
const api=(p)=>fetch('/api/'+p+(p.includes('?')?'&':'?')+'k='+encodeURIComponent(K)).then(r=>r.ok?r.json():null).catch(()=>null);
let speed='medium',maxp={slow:35,medium:55,fast:75,turbo:95},busy=0;
document.querySelectorAll('#spd button').forEach(b=>b.onclick=()=>{speed=b.dataset.s;document.querySelectorAll('#spd button').forEach(x=>x.classList.toggle('on',x==b))});
document.querySelectorAll('#mode button').forEach(b=>b.onclick=()=>{api('mode?m='+b.dataset.m);document.querySelectorAll('#mode button').forEach(x=>x.classList.toggle('on',x==b))});
document.querySelectorAll('#lt button').forEach(b=>b.onclick=()=>{if(b.dataset.l)api('lights?p='+b.dataset.l);else if(b.dataset.n)api('neo?e='+b.dataset.n);else api('neo?e=solid&c='+b.dataset.c)});
document.querySelectorAll('#ears button').forEach(b=>b.onclick=()=>api('ears?g='+b.dataset.e));
['pan','tilt'].forEach(id=>document.getElementById(id).oninput=()=>{if(busy)return;busy=1;setTimeout(()=>busy=0,80);api('head?'+id+'='+document.getElementById(id).value)});
function stopAll(){api('stop');nub.style.left=nub.style.top='73px'}
const joy=document.getElementById('joy'),nub=document.getElementById('nub');let act=false,l=0,r=0,tm=null;
function pos(e){const t=e.touches?e.touches[0]:e,b=joy.getBoundingClientRect(),R=b.width/2;
let x=(t.clientX-b.left-R)/R,y=(t.clientY-b.top-R)/R,m=Math.hypot(x,y);if(m>1){x/=m;y/=m}
nub.style.left=(R+x*R*.62-42)+'px';nub.style.top=(R+y*R*.62-42)+'px';
const f=-y,s=x,p=maxp[speed];l=Math.round(Math.max(-1,Math.min(1,f+s))*p);r=Math.round(Math.max(-1,Math.min(1,f-s))*p)}
function send(){if(act)api('drive?l='+l+'&r='+r+'&h=450')}
function down(e){e.preventDefault();act=true;pos(e);send();tm=setInterval(send,150)}
function move(e){if(act){e.preventDefault();pos(e)}}
function up(){if(!act)return;act=false;clearInterval(tm);stopAll()}
joy.addEventListener('touchstart',down,{passive:false});joy.addEventListener('touchmove',move,{passive:false});joy.addEventListener('touchend',up);
joy.addEventListener('mousedown',down);window.addEventListener('mousemove',move);window.addEventListener('mouseup',up);
aplay.onclick=async()=>{const r=await api('play?u='+encodeURIComponent(url.value));am.textContent=r?r.msg:'error'};
astop.onclick=async()=>{await api('audio_stop');am.textContent='stopped'};
api('favs').then(f=>{if(f&&f.names)f.names.forEach((n,i)=>{const b=document.createElement('button');b.textContent=n;b.onclick=async()=>{const r=await api('play?n='+i);am.textContent=r?r.msg:'error'};favs.appendChild(b)})});
setInterval(async()=>{const a=await api('audio_status');if(a&&a.playing)am.textContent='Playing: '+a.title},3000);
setInterval(async()=>{const s=await api('status');if(!s){st.textContent='offline…';return}
st.innerHTML='Mode: <b>'+s.mode+'</b> · Motor '+(s.battery_v>4?s.battery_v.toFixed(1)+'V ('+s.battery_pct+'%)':'--')+' · Logic '+(s.logic_v>2.5?s.logic_v.toFixed(2)+'V ('+s.logic_pct+'%)':'--')+(s.logic_low?' <b style="color:#e5484d">LOW</b>':'')+(s.battery_cutoff?' <b style="color:#e5484d">LOW – motors off</b>':'')+'<br>Front '+(s.front_cm>=400?'--':s.front_cm+' cm')+' · Rear '+(s.rear_cm>=400?'--':s.rear_cm+' cm')},900);
</script></body></html>)HTML";

static bool GetParam(httpd_req_t* req, const char* key, std::string& out) {
    size_t len = httpd_req_get_url_query_len(req);
    if (len == 0 || len > 1000) return false;
    std::string q(len + 1, '\0');
    if (httpd_req_get_url_query_str(req, q.data(), q.size()) != ESP_OK) return false;
    char val[64];
    if (httpd_query_key_value(q.c_str(), key, val, sizeof(val)) != ESP_OK) return false;
    out = val;
    return true;
}

static int IntParam(httpd_req_t* req, const char* key, int def) {
    std::string s;
    return GetParam(req, key, s) ? atoi(s.c_str()) : def;
}

static bool Authorized(httpd_req_t* req) {
    if (strlen(WEB_CONTROL_PIN) == 0) return true;
    std::string k;
    return GetParam(req, "k", k) && k == WEB_CONTROL_PIN;
}

static esp_err_t Json(httpd_req_t* req, const std::string& body) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body.c_str(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t Deny(httpd_req_t* req) {
    httpd_resp_set_status(req, "401 Unauthorized");
    return httpd_resp_send(req, "{\"error\":\"pin\"}", HTTPD_RESP_USE_STRLEN);
}

#define AUTH() do { if (!Authorized(req)) return Deny(req); } while (0)

static esp_err_t HandleRoot(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kPage, sizeof(kPage) - 1);
}

static esp_err_t HandleStatus(httpd_req_t* req) {
    AUTH();
    return Json(req, MdeeRobot::GetInstance().StatusJson());
}

static esp_err_t HandleDrive(httpd_req_t* req) {
    AUTH();
    MdeeRobot::GetInstance().Drive(IntParam(req, "l", 0), IntParam(req, "r", 0), IntParam(req, "h", 450));
    return Json(req, "{\"ok\":true}");
}

static esp_err_t HandleMove(httpd_req_t* req) {
    AUTH();
    std::string d, s;
    MdeeDir dir; MdeeSpeed sp = MdeeSpeed::kMedium;
    if (!GetParam(req, "dir", d) || !MdeeRobot::ParseDir(d, dir)) return Json(req, "{\"ok\":false}");
    if (GetParam(req, "speed", s)) MdeeRobot::ParseSpeed(s, sp);
    MdeeRobot::GetInstance().Move(dir, sp, IntParam(req, "ms", 1000));
    return Json(req, "{\"ok\":true}");
}

static esp_err_t HandleStop(httpd_req_t* req) {
    AUTH();
    MdeeRobot::GetInstance().Stop();
    return Json(req, "{\"ok\":true}");
}

static esp_err_t HandleMode(httpd_req_t* req) {
    AUTH();
    std::string m; MdeeMode mode;
    if (GetParam(req, "m", m) && MdeeRobot::ParseMode(m, mode)) MdeeRobot::GetInstance().SetMode(mode);
    return Json(req, "{\"ok\":true}");
}

static esp_err_t HandleHead(httpd_req_t* req) {
    AUTH();
    auto& r = MdeeRobot::GetInstance();
    std::string tmp;
    if (GetParam(req, "pan", tmp)) r.SetServo(0, atoi(tmp.c_str()));
    if (GetParam(req, "tilt", tmp)) r.SetServo(1, atoi(tmp.c_str()));
    return Json(req, "{\"ok\":true}");
}

static std::string UrlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else if (s[i] == '+') {
            o += ' ';
        } else {
            o += s[i];
        }
    }
    return o;
}

static bool GetLongParam(httpd_req_t* req, const char* key, std::string& out) {
    size_t len = httpd_req_get_url_query_len(req);
    if (len == 0 || len > 1000) return false;
    std::string q(len + 1, '\0');
    if (httpd_req_get_url_query_str(req, q.data(), q.size()) != ESP_OK) return false;
    std::string val(len + 1, '\0');
    if (httpd_query_key_value(q.c_str(), key, val.data(), val.size()) != ESP_OK) return false;
    val.resize(strlen(val.c_str()));
    out = UrlDecode(val);
    return true;
}

static std::string JsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if (c >= 32) o += c; }
    return o;
}

static esp_err_t HandlePlay(httpd_req_t* req) {
    AUTH();
    std::string url, err;
    if (!GetLongParam(req, "u", url)) {
        int n = IntParam(req, "n", -1);
        if (n < 0 || n >= (int)(sizeof(kAudioFavorites) / sizeof(kAudioFavorites[0]))) return Json(req, "{\"msg\":\"no link\"}");
        url = kAudioFavorites[n].url;
    }
    if (AudioUrlPlayer::GetInstance().Play(url, err)) return Json(req, "{\"msg\":\"Starting... (waits until the robot is idle)\"}");
    return Json(req, "{\"msg\":\"" + JsonEscape(err) + "\"}");
}

static esp_err_t HandleAudioStop(httpd_req_t* req) {
    AUTH();
    AudioUrlPlayer::GetInstance().Stop();
    return Json(req, "{\"ok\":true}");
}

static esp_err_t HandleAudioStatus(httpd_req_t* req) {
    AUTH();
    auto& a = AudioUrlPlayer::GetInstance();
    return Json(req, std::string("{\"playing\":") + (a.IsPlaying() ? "true" : "false") + ",\"title\":\"" + JsonEscape(a.Title()) + "\"}");
}

static esp_err_t HandleFavs(httpd_req_t* req) {
    AUTH();
    std::string s = "{\"names\":[";
    bool first = true;
    for (const auto& f : kAudioFavorites) { if (!first) s += ","; first = false; s += "\"" + JsonEscape(f.name) + "\""; }
    return Json(req, s + "]}");
}

static esp_err_t HandleEars(httpd_req_t* req) {
    AUTH();
    std::string g;
    MdeeEar ear;
    if (GetParam(req, "g", g) && MdeeRobot::ParseEar(g, ear)) MdeeRobot::GetInstance().Ears(ear, 0);
    return Json(req, "{\"ok\":true}");
}

static esp_err_t HandleLights(httpd_req_t* req) {
    AUTH();
    std::string p;
    MdeeLights l = MdeeLights::kOff;
    if (GetParam(req, "p", p)) {
        if (p == "on") l = MdeeLights::kOn;
        else if (p == "chase") l = MdeeLights::kChase;
        else if (p == "blink") l = MdeeLights::kBlink;
        else if (p == "battery") l = MdeeLights::kBatteryBar;
        else if (p == "left") l = MdeeLights::kSignalLeft;
        else if (p == "right") l = MdeeLights::kSignalRight;
    }
    MdeeRobot::GetInstance().SetLights(l);
    return Json(req, "{\"ok\":true}");
}

static esp_err_t HandleNeo(httpd_req_t* req) {
    AUTH();
    std::string e, c;
    MdeeNeo eff = MdeeNeo::kSolid;
    if (GetParam(req, "e", e)) {
        if (e == "off") eff = MdeeNeo::kOff;
        else if (e == "breathe") eff = MdeeNeo::kBreathe;
        else if (e == "rainbow") eff = MdeeNeo::kRainbow;
    }
    int r = 0, g = 60, b = 0;
    if (GetParam(req, "c", c)) sscanf(c.c_str(), "%d%%2C%d%%2C%d", &r, &g, &b);
    if (GetParam(req, "c", c) && r == 0 && g == 60 && b == 0) sscanf(c.c_str(), "%d,%d,%d", &r, &g, &b);
    MdeeRobot::GetInstance().SetNeo(eff, r & 255, g & 255, b & 255);
    return Json(req, "{\"ok\":true}");
}

static void ServerTask(void*) {
    // wait until the station interface has an IP
    esp_netif_t* sta = nullptr;
    esp_netif_ip_info_t ip = {};
    while (true) {
        sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = WEB_CONTROL_PORT;
    cfg.max_uri_handlers = 20;
    cfg.max_uri_len = 1024;
    cfg.max_req_hdr_len = 1024;
    cfg.stack_size = 6144;
    cfg.lru_purge_enable = true;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed (port %d busy?)", WEB_CONTROL_PORT);
        vTaskDelete(nullptr);
        return;
    }
    struct Route { const char* uri; esp_err_t (*fn)(httpd_req_t*); };
    const Route routes[] = {
        {"/", HandleRoot}, {"/api/status", HandleStatus}, {"/api/drive", HandleDrive},
        {"/api/move", HandleMove}, {"/api/stop", HandleStop}, {"/api/mode", HandleMode},
        {"/api/head", HandleHead}, {"/api/play", HandlePlay}, {"/api/audio_stop", HandleAudioStop},
        {"/api/audio_status", HandleAudioStatus}, {"/api/favs", HandleFavs}, {"/api/ears", HandleEars}, {"/api/lights", HandleLights}, {"/api/neo", HandleNeo},
    };
    for (auto& r : routes) {
        httpd_uri_t u = {};
        u.uri = r.uri;
        u.method = HTTP_GET;
        u.handler = r.fn;
        httpd_register_uri_handler(server, &u);
    }
    ESP_LOGI(TAG, "Web control ready: http://" IPSTR "/", IP2STR(&ip.ip));
    vTaskDelete(nullptr);
}

void WebControl::StartWhenOnline() {
    xTaskCreate(ServerTask, "web_ctl", 4096, nullptr, 3, nullptr);
}
