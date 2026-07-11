#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "net_web.h"
#include "config.h"
#include "config_store.h"
#include "mm440.h"
#include "mm440_faults.h"

static WebServer server(80);
static DriveControl* drv = nullptr;
static bool apMode = false;
static uint32_t rebootAt = 0;   // 0 = kein Reboot geplant

bool wifiIsAp() { return apMode; }
String wifiIp() { return apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }

// ------------------------------------------------------------
// Eingebettete Oberfläche (eine Seite, pollt /api/status)
// ------------------------------------------------------------
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MM440 Bridge</title>
<style>
 body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:1rem;max-width:640px;margin-inline:auto}
 h1{font-size:1.2rem} .card{background:#1c1c1e;border-radius:10px;padding:1rem;margin-bottom:1rem}
 .row{display:flex;justify-content:space-between;align-items:center;margin:.4rem 0}
 button{background:#2d6cdf;color:#fff;border:0;border-radius:8px;padding:.6rem 1rem;font-size:1rem;cursor:pointer}
 button.red{background:#c0392b} button.grey{background:#444}
 .big{font-size:2rem;font-weight:700} .st{padding:.15rem .6rem;border-radius:6px;font-weight:600}
 .st.RUNNING{background:#1e8e3e}.st.READY{background:#2d6cdf}.st.FAULT{background:#c0392b}
 .st.MAINS_OFF{background:#555}.st.BOOTING{background:#b07d0f}.st.COMM_LOST{background:#8e44ad}
 input[type=range]{width:100%} input[type=number],input[type=text]{background:#2a2a2c;color:#eee;border:1px solid #444;border-radius:6px;padding:.4rem;width:6rem}
 .warn{color:#e6b400} .err{color:#ff6b6b} label{font-size:.9rem;color:#aaa}
</style></head><body>
<h1>MM440 USS Bridge</h1>
<div style="text-align:right"><a href="/settings" style="color:#2d6cdf">&#9881; Einstellungen</a></div>

<div class="card">
 <div class="row"><span>Zustand</span><span id="state" class="st">–</span></div>
 <div class="row"><span>Istfrequenz</span><span class="big"><span id="hz">0.0</span> Hz</span></div>
 <div class="row"><span>ZSW</span><code id="zsw">0x0000</code></div>
 <div class="row"><span>Motorstrom</span><span><span id="cur">0.0</span> A</span></div>
 <div class="row"><span>Zwischenkreis</span><span><span id="dc">0</span> V</span></div>
 <div class="row"><span>Ausgangsspannung</span><span><span id="uo">0</span> V</span></div>
 <div class="row"><span id="fault" class="err"></span><span id="warntxt" class="warn"></span></div>
 <div class="row"><span id="alarm" class="warn"></span><span id="comm"></span></div>
</div>

<div class="card">
 <div class="row"><span>Netzsch&uuml;tz</span>
  <span><button id="mOn" onclick="cmd({cmd:'mains',on:true})">Ein</button>
        <button id="mOff" class="red" onclick="cmd({cmd:'mains',on:false})">Aus</button></span></div>
 <div class="row"><span>Motor</span>
  <span><button onclick="cmd({cmd:'run',on:true})">Start</button>
        <button class="red" onclick="cmd({cmd:'run',on:false})">Stopp</button>
        <button class="grey" onclick="cmd({cmd:'ack'})">Quittieren</button></span></div>
 <div class="row"><label>Sollwert: <b><span id="spv">0</span> Hz</b></label></div>
 <input type="range" id="sp" min="0" max="50" step="0.5" value="0"
        oninput="document.getElementById('spv').textContent=this.value"
        onchange="cmd({cmd:'setpoint',hz:parseFloat(this.value)})">
 <div class="row"><label><input type="checkbox" id="rev"
        onchange="cmd({cmd:'reverse',on:this.checked})"> Drehrichtung umkehren</label></div>
</div>

<div class="card">
 <b>Parameter</b>
 <div class="row">
  <span>P<input type="number" id="pnu" value="2010" min="0" max="3999">
   Idx <input type="number" id="idx" value="0" min="0" max="255" style="width:4rem"></span>
  <button class="grey" onclick="pread()">Lesen</button></div>
 <div class="row">
  <span>Wert <input type="text" id="pval" value="0">
   <select id="ptype" style="background:#2a2a2c;color:#eee;border:1px solid #444;border-radius:6px;padding:.4rem">
    <option value="int">Ganzzahl</option><option value="float">Float</option></select></span>
  <span><button onclick="pwrite()">Schreiben</button>
        <button class="grey" onclick="cmd({cmd:'save'})" title="P0971=1">EEPROM</button></span></div>
 <div id="pres" class="row"></div>
</div>
<div class="card" style="font-size:.85rem;color:#888" id="stats"></div>
<footer style="text-align:center;margin:1.4rem 0 .3rem;opacity:.55">
 <a href="https://github.com/Emmpunkt/mm440-uss-bridge" target="_blank" rel="noopener" style="color:#999;text-decoration:none;font-size:.78rem">
  <img src="data:image/png;base64,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" alt="Logo" style="height:26px;vertical-align:middle;margin-right:.4rem;opacity:.9">
  github.com/Emmpunkt/mm440-uss-bridge
 </a>
</footer>

<script>
async function cmd(o){await fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});poll();}
// Rohbits <-> Float (IEEE754, 32 Bit) — MM4-Float-Parameter sind immer Doppelwort
function bitsToFloat(u){const b=new ArrayBuffer(4),v=new DataView(b);v.setUint32(0,u>>>0);return v.getFloat32(0);}
function floatToBits(f){const b=new ArrayBuffer(4),v=new DataView(b);v.setFloat32(0,f);return v.getUint32(0);}
function pIsFloat(){return document.getElementById('ptype').value=='float';}
async function pread(){
 const p=document.getElementById('pnu').value,i=document.getElementById('idx').value;
 const r=await(await fetch(`/api/param?pnu=${p}&idx=${i}`)).json();
 if(r.ok){
  const val=pIsFloat()?bitsToFloat(r.value):r.value;
  document.getElementById('pres').textContent=`P${p}[${i}] = ${val}`;
  document.getElementById('pval').value=val;
 }else document.getElementById('pres').textContent=`Fehler 0x${(r.err||0).toString(16)}`;}
async function pwrite(){
 const raw=document.getElementById('pval').value;
 let val,dw;
 if(pIsFloat()){val=floatToBits(parseFloat(raw))>>>0;dw=true;}
 else{const v=Math.trunc(+raw);val=v>>>0;dw=v>0xFFFF;}   // negatives Wort: Zweierkomplement via >>>0
 const b={pnu:+document.getElementById('pnu').value,idx:+document.getElementById('idx').value,value:val,dw:dw};
 const r=await(await fetch('/api/param',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})).json();
 document.getElementById('pres').textContent=r.ok?'Geschrieben (RAM). Für Persistenz: EEPROM.':`Fehler 0x${(r.err||0).toString(16)}`;}
async function poll(){try{
 const s=await(await fetch('/api/status')).json();
 const st=document.getElementById('state');st.textContent=s.state;st.className='st '+s.state;
 document.getElementById('hz').textContent=s.actual_hz.toFixed(1);
 document.getElementById('zsw').textContent='0x'+s.zsw.toString(16).padStart(4,'0');
 document.getElementById('cur').textContent=(s.current_a||0).toFixed(2);
 document.getElementById('dc').textContent=Math.round(s.dclink_v||0);
 document.getElementById('uo').textContent=Math.round(s.outvolt_v||0);
 document.getElementById('fault').textContent=s.fault?('Störung: '+s.fault_text):'';
 document.getElementById('warntxt').textContent=s.alarm?('Warnung: '+s.warn_text):'';
 const sp=document.getElementById('sp');
 if(document.activeElement!==sp){sp.value=s.setpoint_hz;document.getElementById('spv').textContent=s.setpoint_hz;}
 document.getElementById('alarm').textContent='';
 document.getElementById('comm').textContent=s.comm_ok?'':'USS: keine Antwort';
 document.getElementById('comm').className=s.comm_ok?'':'err';
 document.getElementById('stats').textContent=
  `TX ${s.uss.tx} · OK ${s.uss.ok} · Timeout ${s.uss.timeout} · BCC ${s.uss.bcc} · IP ${s.ip}`;
}catch(e){}}
setInterval(poll,1000);poll();
</script></body></html>)HTML";

static const char SETTINGS_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MM440 Einstellungen</title>
<style>
 body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:1rem;max-width:640px;margin-inline:auto}
 h1{font-size:1.2rem} .card{background:#1c1c1e;border-radius:10px;padding:1rem;margin-bottom:1rem}
 label{display:block;font-size:.9rem;color:#aaa;margin:.5rem 0 .2rem}
 input,select{background:#2a2a2c;color:#eee;border:1px solid #444;border-radius:6px;padding:.5rem;width:100%;box-sizing:border-box}
 button{background:#2d6cdf;color:#fff;border:0;border-radius:8px;padding:.6rem 1rem;font-size:1rem;cursor:pointer;margin-top:1rem}
 button.red{background:#c0392b} a{color:#2d6cdf} .msg{margin-top:.6rem}
</style></head><body>
<h1>MM440 Einstellungen</h1><a href="/">&larr; zur&uuml;ck</a>
<div class="card"><b>Ger&auml;t</b>
 <label>Name (HA-Anzeigename)</label><input id="deviceName"></div>
<div class="card"><b>WLAN</b>
 <label>SSID</label><input id="wifiSsid">
 <label>Passwort <span id="wifiPassHint" style="color:#888"></span></label>
 <input id="wifiPass" type="password" placeholder="leer = unver&auml;ndert"></div>
<div class="card"><b>MQTT</b> (Host leer = deaktiviert)
 <label>Host</label><input id="mqttHost">
 <label>Port</label><input id="mqttPort" type="number">
 <label>Benutzer</label><input id="mqttUser">
 <label>Passwort <span id="mqttPassHint" style="color:#888"></span></label>
 <input id="mqttPass" type="password" placeholder="leer = unver&auml;ndert"></div>
<div class="card"><b>USS / Antrieb</b>
 <label>Baud</label><select id="ussBaud">
  <option>9600</option><option>19200</option><option>38400</option>
  <option>57600</option><option>115200</option></select>
 <label>USS-Adresse (0-31)</label><input id="ussSlaveAddr" type="number">
 <label>Bezugsfrequenz P2000 (Hz)</label><input id="refFreqHz" type="number" step="0.1">
 <label>Sollwert min (Hz)</label><input id="setpointMinHz" type="number" step="0.1">
 <label>Sollwert max (Hz)</label><input id="setpointMaxHz" type="number" step="0.1"></div>
<button onclick="save()">Speichern &amp; Neustart</button>
<button class="red" onclick="freset()" style="float:right">Werkseinstellungen</button>
<div id="msg" class="msg"></div>
<script>
const F=['deviceName','wifiSsid','mqttHost','mqttPort','mqttUser','ussBaud','ussSlaveAddr','refFreqHz','setpointMinHz','setpointMaxHz'];
async function load(){const c=await(await fetch('/api/config')).json();
 for(const k of F){const e=document.getElementById(k);if(e)e.value=c[k];}
 document.getElementById('wifiPassHint').textContent=c.wifiPassSet?'(gesetzt)':'';
 document.getElementById('mqttPassHint').textContent=c.mqttPassSet?'(gesetzt)':'';}
async function save(){const b={};
 for(const k of F){const e=document.getElementById(k);if(e)b[k]=e.value;}
 b.mqttPort=+b.mqttPort;b.ussBaud=+b.ussBaud;b.ussSlaveAddr=+b.ussSlaveAddr;
 b.refFreqHz=+b.refFreqHz;b.setpointMinHz=+b.setpointMinHz;b.setpointMaxHz=+b.setpointMaxHz;
 const wp=document.getElementById('wifiPass').value;if(wp)b.wifiPass=wp;
 const mp=document.getElementById('mqttPass').value;if(mp)b.mqttPass=mp;
 const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
 const j=await r.json();
 document.getElementById('msg').textContent=j.ok?'Gespeichert. Neustart läuft …':('Fehler: '+(j.err||r.status));}
async function freset(){if(!confirm('Wirklich auf Werkseinstellungen zurücksetzen?'))return;
 await fetch('/api/factoryreset',{method:'POST'});
 document.getElementById('msg').textContent='Zurückgesetzt. Neustart (AP-Modus) …';}
load();
</script></body></html>)HTML";

// ------------------------------------------------------------
// Handler
// ------------------------------------------------------------
static void handleStatus() {
  JsonDocument d;
  d["state"] = driveStateName(drv->state());
  d["mains"] = drv->mainsRelay();
  d["running"] = drv->running();
  d["fault"] = drv->fault();
  d["alarm"] = drv->alarm();
  d["comm_ok"] = drv->commOk();
  d["actual_hz"] = drv->actualHz();
  d["setpoint_hz"] = drv->setpointHz();
  d["zsw"] = drv->zsw();
  d["current_a"] = drv->currentA();
  d["dclink_v"]  = drv->dcLinkV();
  d["outvolt_v"] = drv->outVoltV();
  d["fault_num"] = drv->faultNum();
  d["warn_num"]  = drv->warnNum();
  d["fault_text"] = drv->fault() ? faultLabel(drv->faultNum()) : String("");
  d["warn_text"]  = drv->alarm() ? warnLabel(drv->warnNum())  : String("");
  d["ip"] = wifiIp();
  JsonObject u = d["uss"].to<JsonObject>();
  u["tx"] = drv->ussStats().txCount;
  u["ok"] = drv->ussStats().rxOk;
  u["timeout"] = drv->ussStats().timeouts;
  u["bcc"] = drv->ussStats().bccErrors;
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void handleCmd() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false}"); return;
  }
  const char* cmd = d["cmd"] | "";
  bool ok = true;
  if      (!strcmp(cmd, "mains"))    { d["on"].as<bool>() ? drv->mainsOn() : drv->mainsOff(); }
  else if (!strcmp(cmd, "run"))      { drv->run(d["on"].as<bool>()); }
  else if (!strcmp(cmd, "setpoint")) { drv->setSetpointHz(d["hz"].as<float>()); }
  else if (!strcmp(cmd, "reverse"))  { drv->reverse(d["on"].as<bool>()); }
  else if (!strcmp(cmd, "ack"))      { drv->ackFault(); }
  else if (!strcmp(cmd, "save"))     { uint16_t e; ok = drv->saveToEeprom(e); }
  else ok = false;
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleParamGet() {
  uint16_t pnu = server.arg("pnu").toInt();
  uint8_t  idx = server.arg("idx").toInt();
  uint32_t val; uint16_t err;
  JsonDocument d;
  if (drv->readParam(pnu, idx, val, err)) { d["ok"] = true; d["value"] = val; }
  else { d["ok"] = false; d["err"] = err; }
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void handleParamPost() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false}"); return;
  }
  uint32_t err32; uint16_t err;
  bool ok = drv->writeParam(d["pnu"].as<uint16_t>(), d["idx"].as<uint8_t>(),
                            d["value"].as<uint32_t>(), d["dw"].as<bool>(), err);
  (void)err32;
  JsonDocument r; r["ok"] = ok; if (!ok) r["err"] = err;
  String out; serializeJson(r, out);
  server.send(200, "application/json", out);
}

static void handleConfigGet() {
  const Config& c = configGet();
  JsonDocument d;
  d["deviceName"]   = c.deviceName;
  d["wifiSsid"]     = c.wifiSsid;
  d["wifiPassSet"]  = strlen(c.wifiPass) > 0;
  d["mqttHost"]     = c.mqttHost;
  d["mqttPort"]     = c.mqttPort;
  d["mqttUser"]     = c.mqttUser;
  d["mqttPassSet"]  = strlen(c.mqttPass) > 0;
  d["ussBaud"]      = c.ussBaud;
  d["ussSlaveAddr"] = c.ussSlaveAddr;
  d["refFreqHz"]    = c.refFreqHz;
  d["setpointMinHz"]= c.setpointMinHz;
  d["setpointMaxHz"]= c.setpointMaxHz;
  d["language"]     = c.language;
  d["hostname"]     = configHostname();
  d["mqttBase"]     = configMqttBase();
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void handleConfigPost() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"JSON\"}"); return;
  }
  Config c = configGet();          // Basis: aktuelle Werte (für write-only Passwörter)

  auto setStr = [](char* dst, size_t n, JsonVariant v) {
    if (!v.isNull()) { strncpy(dst, v.as<const char*>(), n - 1); dst[n - 1] = '\0'; }
  };
  setStr(c.deviceName, sizeof(c.deviceName), d["deviceName"]);
  setStr(c.wifiSsid,   sizeof(c.wifiSsid),   d["wifiSsid"]);
  setStr(c.mqttHost,   sizeof(c.mqttHost),   d["mqttHost"]);
  setStr(c.mqttUser,   sizeof(c.mqttUser),   d["mqttUser"]);
  // Passwörter write-only: nur übernehmen wenn nicht-leer gesendet
  if (!d["wifiPass"].isNull() && strlen(d["wifiPass"]) > 0)
    setStr(c.wifiPass, sizeof(c.wifiPass), d["wifiPass"]);
  if (!d["mqttPass"].isNull() && strlen(d["mqttPass"]) > 0)
    setStr(c.mqttPass, sizeof(c.mqttPass), d["mqttPass"]);
  if (!d["mqttPort"].isNull())      c.mqttPort      = d["mqttPort"].as<uint16_t>();
  if (!d["ussBaud"].isNull())       c.ussBaud       = d["ussBaud"].as<uint32_t>();
  if (!d["ussSlaveAddr"].isNull())  c.ussSlaveAddr  = d["ussSlaveAddr"].as<uint8_t>();
  if (!d["refFreqHz"].isNull())     c.refFreqHz     = d["refFreqHz"].as<float>();
  if (!d["setpointMinHz"].isNull()) c.setpointMinHz = d["setpointMinHz"].as<float>();
  if (!d["setpointMaxHz"].isNull()) c.setpointMaxHz = d["setpointMaxHz"].as<float>();
  if (!d["language"].isNull()) {
    const char* l = d["language"].as<const char*>();
    strncpy(c.language, (l && strcmp(l, "en") == 0) ? "en" : "de", sizeof(c.language) - 1);
    c.language[sizeof(c.language) - 1] = '\0';
  }

  // Validierung
  const char* err = nullptr;
  if (strlen(c.deviceName) == 0) err = "Name leer";
  else if (strlen(c.mqttHost) > 0 && c.mqttPort < 1) err = "Port ungueltig";
  else if (!(c.ussBaud == 9600 || c.ussBaud == 19200 || c.ussBaud == 38400 ||
             c.ussBaud == 57600 || c.ussBaud == 115200)) err = "Baud ungueltig";
  else if (c.ussSlaveAddr > 31) err = "Adresse 0-31";
  else if (!(c.refFreqHz > 0 && c.refFreqHz <= 650)) err = "Bezugsfreq ungueltig";
  else if (!(c.setpointMinHz >= 0 && c.setpointMaxHz > c.setpointMinHz &&
             c.setpointMaxHz <= 650)) err = "Sollwertgrenzen ungueltig";
  if (err) {
    JsonDocument r; r["ok"] = false; r["err"] = err;
    String out; serializeJson(r, out);
    server.send(400, "application/json", out); return;
  }
  if (!configSave(c)) {
    server.send(500, "application/json", "{\"ok\":false,\"err\":\"NVS\"}"); return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
  rebootAt = millis() + 500;       // nach dem Senden neu starten
}

static void handleFactoryReset() {
  configFactoryReset();
  server.send(200, "application/json", "{\"ok\":true}");
  rebootAt = millis() + 500;
}

// ------------------------------------------------------------
void webBegin(DriveControl& drive, const Config& c) {
  drv = &drive;

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(configHostname().c_str());
  if (strlen(c.wifiSsid) > 0 && strcmp(c.wifiSsid, "CHANGE_ME") != 0) {
    WiFi.begin(c.wifiSsid, c.wifiPass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
  }

  server.on("/", HTTP_GET, [](){
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/settings", HTTP_GET, [](){
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", SETTINGS_HTML);
  });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/factoryreset", HTTP_POST, handleFactoryReset);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.on("/api/param", HTTP_GET, handleParamGet);
  server.on("/api/param", HTTP_POST, handleParamPost);
  server.begin();
}

void webLoop() {
  server.handleClient();
  if (rebootAt && millis() > rebootAt) { delay(50); ESP.restart(); }
}
