#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "net_web.h"
#include "config.h"
#include "config_store.h"
#include "mm440.h"

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
 document.getElementById('alarm').textContent=s.alarm?'⚠ Warnung aktiv':'';
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

  server.on("/", HTTP_GET, [](){ server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/settings", HTTP_GET, [](){ server.send_P(200, "text/html", SETTINGS_HTML); });
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
