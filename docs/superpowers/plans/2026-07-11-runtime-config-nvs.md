# Runtime-Konfiguration (NVS + Settings-Seite) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Alle Betriebsparameter (WLAN, MQTT, USS/Antrieb, Gerätename) zur Laufzeit über eine Web-Settings-Seite ändern und im NVS persistieren — kein Neuflashen mehr.

**Architecture:** Zentrale `struct Config` als Single-Source-of-Truth, beim Boot aus NVS (`Preferences`, Blob) geladen; `config.h`-Makros sind Factory-Defaults. Alle Module (`drive_control`, `net_web`, `net_mqtt`) bekommen `const Config&` und lesen daraus statt aus Makros. Settings-Seite unter `/settings` + REST `GET/POST /api/config`; Speichern → NVS → Reboot.

**Tech Stack:** ESP32-C3, PlatformIO (Arduino), `Preferences` (NVS), `WebServer`, `ArduinoJson`, `PubSubClient`.

## Global Constraints

- Sprache Code/Kommentare: **Deutsch**. Antworten knapp.
- Board/Flash: `pio run` (bauen), `pio run -t upload` (USB /dev/ttyACM0). `pio` liegt in `~/Dokumente/T-Display-Sattelite/.venv/bin/pio`.
- Bridge-IP im LAN aktuell: **<BRIDGE-IP>–…**, konkret zuletzt **<BRIDGE-IP>** (DHCP; per `curl http://<ip>/api/status` oder Titel „MM440 USS Bridge" auffindbar; kein mDNS).
- **Kein Unit-Test-Framework.** Verifikation je Task = bauen + flashen + `curl`/Verhalten. Nach jedem Flash re-enumeriert der native USB-CDC; ~10 s auf WLAN-Reconnect warten (`for i in $(seq 1 20); do curl -s -m2 http://<ip>/api/status >/dev/null && break; sleep 1; done`).
- **Sicherheit:** `setup()` setzt Relais/DE vor allem anderen inaktiv — diese Reihenfolge NICHT verändern. Passwörter write-only.
- Passwortfeld im POST: leer = altes behalten, nicht leer = ersetzen.
- Feature-Branch anlegen vor Implementierung: `git checkout -b feature/runtime-config`.
- `config.h` steht in `.gitignore`; Änderungen an Default-Werten parallel in `include/config.example.h` pflegen.

---

## Datei-Struktur

- **Neu** `include/config_store.h` — `struct Config`, API-Deklarationen.
- **Neu** `src/config_store.cpp` — Load/Save/FactoryReset, Defaults, Sanitizer.
- **Ändern** `src/main.cpp` — `configLoad()` zuerst, Config an Module reichen.
- **Ändern** `include/drive_control.h`, `src/drive_control.cpp` — `begin(const Config&)`, Antriebswerte aus cfg.
- **Ändern** `include/net_web.h`, `src/net_web.cpp` — `webBegin(drive, cfg)`, `/api/config` GET/POST, `/settings`, `/api/factoryreset`, Link.
- **Ändern** `include/net_mqtt.h`, `src/net_mqtt.cpp` — `mqttBegin(drive, cfg)`, Host/Port/User/Pass/Base/Name aus cfg.
- **Ändern** `include/config.example.h` — Kommentar „nur Defaults".

---

### Task 1: config_store — Struct, Laden/Speichern, Sanitizer

**Files:**
- Create: `include/config_store.h`
- Create: `src/config_store.cpp`
- Modify: `src/main.cpp` (Aufruf `configLoad()` in `setup()` ganz vorne nach den Failsafe-Pins)

**Interfaces:**
- Produces:
  - `struct Config { uint16_t magic; uint8_t version; char deviceName[32]; char wifiSsid[33]; char wifiPass[64]; char mqttHost[64]; uint16_t mqttPort; char mqttUser[33]; char mqttPass[64]; uint32_t ussBaud; uint8_t ussSlaveAddr; float refFreqHz; float setpointMinHz; float setpointMaxHz; };`
  - `void configLoad();`
  - `Config& configGet();`
  - `bool configSave(const Config& c);`
  - `void configFactoryReset();`
  - `String configHostname();`  // sanitisiert aus deviceName, Fallback "mm440-bridge"
  - `String configMqttBase();`  // sanitisiert aus deviceName, Fallback "mm440"

- [ ] **Step 1: Branch anlegen**

```bash
cd /home/emmpunkt/Dokumente/mm440-uss-bridge
git checkout -b feature/runtime-config
```

- [ ] **Step 2: `include/config_store.h` schreiben**

```cpp
#pragma once
#include <Arduino.h>

// Zentrale Laufzeit-Konfiguration (Single-Source-of-Truth).
// Beim Boot aus NVS geladen; Defaults kommen aus config.h.
struct Config {
  uint16_t magic;          // CONFIG_MAGIC, validiert NVS-Inhalt
  uint8_t  version;        // CONFIG_VERSION
  char     deviceName[32]; // HA-Anzeigename; Quelle für Hostname/Topic
  char     wifiSsid[33];
  char     wifiPass[64];
  char     mqttHost[64];   // leer => MQTT deaktiviert
  uint16_t mqttPort;
  char     mqttUser[33];
  char     mqttPass[64];
  uint32_t ussBaud;
  uint8_t  ussSlaveAddr;
  float    refFreqHz;
  float    setpointMinHz;
  float    setpointMaxHz;
};

void    configLoad();               // NVS -> globales Config; ungültig => Defaults
Config& configGet();                // Zugriff auf geladenes Config
bool    configSave(const Config& c);// Config -> NVS (atomar)
void    configFactoryReset();       // NVS-Namespace leeren
String  configHostname();           // aus deviceName, [a-z0-9-], Fallback mm440-bridge
String  configMqttBase();           // aus deviceName, [a-z0-9-], Fallback mm440
```

- [ ] **Step 3: `src/config_store.cpp` schreiben**

```cpp
#include "config_store.h"
#include "config.h"
#include <Preferences.h>

static constexpr uint16_t CONFIG_MAGIC   = 0x4D34; // "M4"
static constexpr uint8_t  CONFIG_VERSION = 1;
static constexpr const char* NVS_NS  = "mm440cfg";
static constexpr const char* NVS_KEY = "cfg";

static Config g_cfg;

static void copyStr(char* dst, size_t n, const char* src) {
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

static void fillDefaults(Config& c) {
  memset(&c, 0, sizeof(c));
  c.magic   = CONFIG_MAGIC;
  c.version = CONFIG_VERSION;
  copyStr(c.deviceName, sizeof(c.deviceName), HA_DEVICE_NAME);
  // WLAN: "CHANGE_ME" bewusst übernehmen -> löst AP-Fallback aus
  copyStr(c.wifiSsid, sizeof(c.wifiSsid), WIFI_SSID);
  copyStr(c.wifiPass, sizeof(c.wifiPass), WIFI_PASS);
  copyStr(c.mqttHost, sizeof(c.mqttHost), MQTT_HOST);
  c.mqttPort = MQTT_PORT;
  copyStr(c.mqttUser, sizeof(c.mqttUser), MQTT_USER);
  copyStr(c.mqttPass, sizeof(c.mqttPass), MQTT_PASSWORD);
  c.ussBaud       = USS_BAUD;
  c.ussSlaveAddr  = USS_SLAVE_ADDR;
  c.refFreqHz     = MM440_REF_FREQ_HZ;
  c.setpointMinHz = SETPOINT_MIN_HZ;
  c.setpointMaxHz = SETPOINT_MAX_HZ;
}

void configLoad() {
  Preferences p;
  p.begin(NVS_NS, true);              // read-only
  Config tmp;
  size_t got = p.getBytes(NVS_KEY, &tmp, sizeof(tmp));
  p.end();
  if (got == sizeof(tmp) && tmp.magic == CONFIG_MAGIC &&
      tmp.version == CONFIG_VERSION) {
    g_cfg = tmp;
  } else {
    fillDefaults(g_cfg);
  }
}

Config& configGet() { return g_cfg; }

bool configSave(const Config& c) {
  Config w = c;
  w.magic = CONFIG_MAGIC;
  w.version = CONFIG_VERSION;
  Preferences p;
  if (!p.begin(NVS_NS, false)) return false;
  size_t n = p.putBytes(NVS_KEY, &w, sizeof(w));
  p.end();
  if (n == sizeof(w)) { g_cfg = w; return true; }
  return false;
}

void configFactoryReset() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.clear();
  p.end();
}

static String sanitize(const char* name, const char* fallback) {
  String s;
  for (const char* q = name; *q; ++q) {
    char ch = *q;
    if (ch >= 'A' && ch <= 'Z') ch += 32;          // lower
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) s += ch;
    else if (ch == '-' || ch == ' ' || ch == '_') s += '-';
    // andere Zeichen weglassen
  }
  while (s.startsWith("-")) s.remove(0, 1);
  while (s.endsWith("-"))   s.remove(s.length() - 1);
  if (s.length() == 0) s = fallback;
  if (s.length() > 31) s = s.substring(0, 31);
  return s;
}

String configHostname() { return sanitize(g_cfg.deviceName, "mm440-bridge"); }
String configMqttBase() { return sanitize(g_cfg.deviceName, "mm440"); }
```

- [ ] **Step 4: `src/main.cpp` — `configLoad()` einfügen**

In `setup()` direkt NACH den Failsafe-Pins (`digitalWrite(PIN_RS485_DE, LOW);`) und VOR `Serial.begin`… bzw. spätestens vor `drive.begin()` einfügen:

```cpp
#include "config_store.h"   // oben zu den Includes
```
```cpp
  configLoad();             // NVS -> Config, vor drive/web/mqtt
```

(Die `*.begin()`-Aufrufe bleiben in Task 3/4 unverändert bis zur Umstellung.)

- [ ] **Step 5: Bauen**

```bash
pio run
```
Expected: `[SUCCESS]`, keine Fehler.

- [ ] **Step 6: Flashen und Regressionscheck**

```bash
pio run -t upload
```
Danach ~10 s warten, dann:
```bash
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/api/status >/dev/null && break; sleep 1; done
curl -s -m3 http://<BRIDGE-IP>/api/status
```
Expected: JSON-Status wie bisher (Bridge bootet unverändert, `configLoad()` ohne Sichtbarkeit — noch keine Verhaltensänderung).

- [ ] **Step 7: Commit**

```bash
git add include/config_store.h src/config_store.cpp src/main.cpp
git commit -m "config_store: Config-Struct + NVS-Load/Save + Sanitizer

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `GET /api/config` (lesend, ohne Passwörter)

**Files:**
- Modify: `src/net_web.cpp` (neuer Handler + Route)

**Interfaces:**
- Consumes: `configGet()`, `configHostname()`, `configMqttBase()` (Task 1).
- Produces: HTTP `GET /api/config` → JSON aller Werte außer Passwörtern, plus `wifiPassSet`/`mqttPassSet` (bool), `hostname`, `mqttBase`.

- [ ] **Step 1: Include ergänzen** in `src/net_web.cpp` (oben):

```cpp
#include "config_store.h"
```

- [ ] **Step 2: Handler einfügen** (vor `webBegin`):

```cpp
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
```

- [ ] **Step 3: Route registrieren** in `webBegin` (bei den anderen `server.on(...)`):

```cpp
  server.on("/api/config", HTTP_GET, handleConfigGet);
```

- [ ] **Step 4: Bauen + flashen**

```bash
pio run -t upload
```

- [ ] **Step 5: Verifizieren**

```bash
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/api/config >/dev/null && break; sleep 1; done
curl -s -m3 http://<BRIDGE-IP>/api/config
```
Expected: JSON mit Default-Werten (`deviceName":"MM440 Umrichter"`, `ussBaud":38400`, `refFreqHz":50`, …), **keine** `wifiPass`/`mqttPass`-Felder, dafür `wifiPassSet`/`mqttPassSet`.

- [ ] **Step 6: Commit**

```bash
git add src/net_web.cpp
git commit -m "net_web: GET /api/config (ohne Passwörter)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: drive_control aus Config speisen

**Files:**
- Modify: `include/drive_control.h` (Signatur + Member)
- Modify: `src/drive_control.cpp` (begin, Skalierung)

**Interfaces:**
- Consumes: `struct Config` (Task 1).
- Produces: `void DriveControl::begin(const Config& c);` — nutzt `c.ussBaud`, `c.ussSlaveAddr`, speichert `refFreqHz`/`setpointMin/MaxHz` als Member.

- [ ] **Step 1: `include/drive_control.h` anpassen**

Include ergänzen: `#include "config_store.h"`. Signatur ändern zu `void begin(const Config& c);`. Neue private Member ergänzen:
```cpp
  float _refFreqHz = 50.0f;
  float _setpointMinHz = 0.0f;
  float _setpointMaxHz = 50.0f;
```

- [ ] **Step 2: `src/drive_control.cpp::begin` anpassen**

```cpp
void DriveControl::begin(const Config& c) {
  pinMode(PIN_RELAY, OUTPUT);
  relayWrite(false);
  _refFreqHz     = c.refFreqHz;
  _setpointMinHz = c.setpointMinHz;
  _setpointMaxHz = c.setpointMaxHz;
  _uss.begin(Serial1, c.ussBaud, PIN_RS485_TX, PIN_RS485_RX,
             PIN_RS485_DE, c.ussSlaveAddr);
  _uss.setControl(STW::READY, 0);
}
```

- [ ] **Step 3: Makros durch Member ersetzen** in `src/drive_control.cpp`:
  - `actualHz()`: `MM440_REF_FREQ_HZ` → `_refFreqHz`
  - `setSetpointHz()`: `SETPOINT_MIN_HZ, SETPOINT_MAX_HZ` → `_setpointMinHz, _setpointMaxHz`
  - `applyControlWord()`: `MM440_REF_FREQ_HZ` → `_refFreqHz`

- [ ] **Step 4: `src/main.cpp` anpassen** — Aufruf `drive.begin();` → `drive.begin(configGet());`

- [ ] **Step 5: Bauen + flashen**

```bash
pio run -t upload
```

- [ ] **Step 6: Verifizieren (Verhalten unverändert)**

```bash
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/api/status >/dev/null && break; sleep 1; done
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"mains","on":true}'; sleep 7
curl -s -m3 "http://<BRIDGE-IP>/api/param?pnu=2010&idx=0"    # erwartet value:8
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"setpoint","hz":5}' >/dev/null
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"run","on":true}' >/dev/null; sleep 4
curl -s -m3 http://<BRIDGE-IP>/api/status   # erwartet actual_hz ~5.0
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"run","on":false}' >/dev/null
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"mains","on":false}' >/dev/null
```
Expected: P2010=8, actual_hz ≈ 5.0 (Defaults == alte Makros, keine Regression).

- [ ] **Step 7: Commit**

```bash
git add include/drive_control.h src/drive_control.cpp src/main.cpp
git commit -m "drive_control: USS/Antriebswerte aus Config

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: net_web (WLAN) + net_mqtt aus Config speisen

**Files:**
- Modify: `include/net_web.h`, `src/net_web.cpp`
- Modify: `include/net_mqtt.h`, `src/net_mqtt.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `configGet()`, `configHostname()`, `configMqttBase()`.
- Produces: `void webBegin(DriveControl& drive, const Config& c);`, `void mqttBegin(DriveControl& drive, const Config& c);`

- [ ] **Step 1: `include/net_web.h` + `include/net_mqtt.h`** — Signaturen erweitern:
```cpp
void webBegin(DriveControl& drive, const Config& c);
void mqttBegin(DriveControl& drive, const Config& c);
```
(Include `#include "config_store.h"` in beiden Headern ergänzen.)

- [ ] **Step 2: `src/net_web.cpp::webBegin`** — WLAN aus cfg:

```cpp
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
  // ... (server.on-Routen unverändert lassen)
```
Der Rest von `webBegin` (Routen + `server.begin()`) bleibt.

- [ ] **Step 3: `src/net_mqtt.cpp`** — Modul auf cfg umstellen.

Statische Kopien am Dateikopf ergänzen:
```cpp
static char mqttHost[64]; static uint16_t mqttPort;
static char mqttUser[33]; static char mqttPass[64];
static String baseTopic; static String haName;
```
`topic()` auf `baseTopic` umstellen:
```cpp
static String topic(const char* sub) { return baseTopic + "/" + sub; }
```
`addDevice()`: `dev["name"] = haName.c_str();`
`connect()`: `mqtt.connect(devId, mqttUser, mqttPass, will.c_str(), 0, true, "offline")`
`mqttBegin` neu:
```cpp
void mqttBegin(DriveControl& drive, const Config& c) {
  drv = &drive;
  if (strlen(c.mqttHost) == 0) { enabled = false; return; }
  enabled = true;
  strncpy(mqttHost, c.mqttHost, sizeof(mqttHost)-1);
  strncpy(mqttUser, c.mqttUser, sizeof(mqttUser)-1);
  strncpy(mqttPass, c.mqttPass, sizeof(mqttPass)-1);
  mqttPort = c.mqttPort;
  baseTopic = configMqttBase();
  haName = c.deviceName;
  snprintf(devId, sizeof(devId), "mm440_%06lx",
           (unsigned long)(ESP.getEfuseMac() & 0xFFFFFF));
  mqtt.setServer(mqttHost, mqttPort);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(onMessage);
}
```
(`MQTT_USER`/`MQTT_PASSWORD`/`MQTT_HOST`/`MQTT_BASE`/`HA_DEVICE_NAME`-Referenzen in net_mqtt.cpp entfernen.)

- [ ] **Step 4: `src/main.cpp`** — Aufrufe anpassen:
```cpp
  webBegin(drive, configGet());
  mqttBegin(drive, configGet());
```

- [ ] **Step 5: Bauen + flashen**
```bash
pio run -t upload
```

- [ ] **Step 6: Verifizieren**
```bash
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/api/status >/dev/null && break; sleep 1; done
curl -s -m3 http://<BRIDGE-IP>/api/status
```
Expected: Bridge verbindet weiterhin mit WLAN (Defaults aus config.h), MQTT bleibt aus (Host leer). Kein Regress.

- [ ] **Step 7: Commit**
```bash
git add include/net_web.h src/net_web.cpp include/net_mqtt.h src/net_mqtt.cpp src/main.cpp
git commit -m "net_web/net_mqtt: WLAN + MQTT aus Config

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: `POST /api/config` — validieren, speichern, Reboot

**Files:**
- Modify: `src/net_web.cpp`

**Interfaces:**
- Consumes: `configGet()`, `configSave()`.
- Produces: HTTP `POST /api/config` (JSON) → `{"ok":true}` + Reboot, oder `{"ok":false,"err":"..."}` (HTTP 400).

- [ ] **Step 1: Reboot-Flag + Handler** in `src/net_web.cpp` einfügen:

```cpp
static uint32_t rebootAt = 0;   // 0 = kein Reboot geplant

static void handleConfigPost() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"JSON\"}"); return;
  }
  Config c = configGet();          // Basis: aktuelle Werte (für write-only Passwörter)

  auto setStr = [](char* dst, size_t n, JsonVariant v) {
    if (!v.isNull()) { strncpy(dst, v.as<const char*>(), n-1); dst[n-1]='\0'; }
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
  else if (strlen(c.mqttHost) > 0 && (c.mqttPort < 1)) err = "Port ungueltig";
  else if (!(c.ussBaud==9600||c.ussBaud==19200||c.ussBaud==38400||
             c.ussBaud==57600||c.ussBaud==115200)) err = "Baud ungueltig";
  else if (c.ussSlaveAddr > 31) err = "Adresse 0-31";
  else if (!(c.refFreqHz > 0 && c.refFreqHz <= 650)) err = "Bezugsfreq ungueltig";
  else if (!(c.setpointMinHz >= 0 && c.setpointMaxHz > c.setpointMinHz &&
             c.setpointMaxHz <= 650)) err = "Sollwertgrenzen ungueltig";
  if (err) {
    JsonDocument r; r["ok"]=false; r["err"]=err;
    String out; serializeJson(r, out);
    server.send(400, "application/json", out); return;
  }
  if (!configSave(c)) { server.send(500, "application/json", "{\"ok\":false,\"err\":\"NVS\"}"); return; }
  server.send(200, "application/json", "{\"ok\":true}");
  rebootAt = millis() + 500;       // nach dem Senden neu starten
}
```

- [ ] **Step 2: Reboot in `webLoop()` auslösen**

In `webLoop()` (existiert in net_web.cpp; ruft `server.handleClient()`) ergänzen:
```cpp
  if (rebootAt && millis() > rebootAt) { delay(50); ESP.restart(); }
```

- [ ] **Step 3: Route registrieren** in `webBegin`:
```cpp
  server.on("/api/config", HTTP_POST, handleConfigPost);
```

- [ ] **Step 4: Bauen + flashen**
```bash
pio run -t upload
```

- [ ] **Step 5: Verifizieren — gültige Änderung + Reboot**
```bash
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/api/config >/dev/null && break; sleep 1; done
curl -s -m3 -X POST http://<BRIDGE-IP>/api/config -H 'Content-Type: application/json' -d '{"refFreqHz":55}'
sleep 12   # Reboot + Reconnect
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/api/config >/dev/null && break; sleep 1; done
curl -s -m3 http://<BRIDGE-IP>/api/config    # refFreqHz muss 55 sein (aus NVS)
```
Expected: erster POST `{"ok":true}`, nach Reboot `refFreqHz":55` — persistiert.

- [ ] **Step 6: Verifizieren — ungültige Eingabe**
```bash
curl -s -m3 -o /dev/null -w "%{http_code}\n" -X POST http://<BRIDGE-IP>/api/config -H 'Content-Type: application/json' -d '{"ussBaud":12345}'
```
Expected: `400` (kein Speichern/Reboot).

- [ ] **Step 7: Zurücksetzen auf 50 + Commit**
```bash
curl -s -m3 -X POST http://<BRIDGE-IP>/api/config -H 'Content-Type: application/json' -d '{"refFreqHz":50}'; sleep 12
git add src/net_web.cpp
git commit -m "net_web: POST /api/config mit Validierung + Reboot

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: `/settings`-Seite + Link auf Hauptseite

**Files:**
- Modify: `src/net_web.cpp` (neue PROGMEM-Seite `SETTINGS_HTML`, Route, Link in `INDEX_HTML`)

**Interfaces:**
- Consumes: `GET /api/config`, `POST /api/config` (Task 2/5).
- Produces: `GET /settings` → HTML-Formular.

- [ ] **Step 1: `SETTINGS_HTML` (PROGMEM) einfügen** in `src/net_web.cpp` (nach `INDEX_HTML`):

```cpp
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
```

- [ ] **Step 2: Route** in `webBegin`:
```cpp
  server.on("/settings", HTTP_GET, [](){ server.send_P(200, "text/html", SETTINGS_HTML); });
```

- [ ] **Step 3: Link auf Hauptseite** — in `INDEX_HTML` direkt nach `<h1>MM440 USS Bridge</h1>` ergänzen:
```html
<div style="text-align:right"><a href="/settings" style="color:#2d6cdf">&#9881; Einstellungen</a></div>
```

- [ ] **Step 4: Bauen + flashen**
```bash
pio run -t upload
```

- [ ] **Step 5: Verifizieren**
```bash
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/settings >/dev/null && break; sleep 1; done
curl -s -m3 http://<BRIDGE-IP>/settings | grep -c "MM440 Einstellungen"   # >=1
```
Zusätzlich im Browser `http://<BRIDGE-IP>/settings` öffnen: Formular ist vorbefüllt, Passwortfelder leer mit „(gesetzt)"-Hinweis. Testweise `deviceName` ändern → Speichern → nach Reboot in HA/`/api/config` prüfen.

- [ ] **Step 6: Commit**
```bash
git add src/net_web.cpp
git commit -m "net_web: /settings-Seite + Link auf Hauptseite

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: `POST /api/factoryreset`

**Files:**
- Modify: `src/net_web.cpp`

**Interfaces:**
- Consumes: `configFactoryReset()` (Task 1), `rebootAt` (Task 5).
- Produces: HTTP `POST /api/factoryreset` → NVS leeren, `{"ok":true}`, Reboot.

- [ ] **Step 1: Handler + Route** in `src/net_web.cpp`:
```cpp
static void handleFactoryReset() {
  configFactoryReset();
  server.send(200, "application/json", "{\"ok\":true}");
  rebootAt = millis() + 500;
}
```
In `webBegin`:
```cpp
  server.on("/api/factoryreset", HTTP_POST, handleFactoryReset);
```

- [ ] **Step 2: Bauen + flashen**
```bash
pio run -t upload
```

- [ ] **Step 3: Verifizieren (Vorsicht: setzt WLAN auf Default zurück)**

Nur ausführen, wenn config.h-Default `WIFI_SSID` weiterhin auf euer LAN zeigt (dann verbindet die Bridge nach Reset wieder normal). Andernfalls kommt sie im AP `MM440-Bridge` hoch — dann über den AP neu konfigurieren.
```bash
curl -s -m3 -X POST http://<BRIDGE-IP>/api/factoryreset
sleep 12
# entweder wieder im LAN:
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/api/config >/dev/null && break; sleep 1; done
curl -s -m3 http://<BRIDGE-IP>/api/config   # Default-Werte
# oder AP MM440-Bridge scannen:
nmcli -f SSID dev wifi list --rescan yes | grep -c MM440-Bridge
```
Expected: Config auf Defaults zurück (bzw. AP-Modus).

- [ ] **Step 4: Commit**
```bash
git add src/net_web.cpp
git commit -m "net_web: POST /api/factoryreset

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 8: End-to-End mit MQTT/HA + Doku

**Files:**
- Modify: `include/config.example.h` (Kommentar), `CLAUDE.md` (Stand)

**Interfaces:**
- Consumes: alle vorherigen Tasks.

- [ ] **Step 1: MQTT live einrichten (Broker-Daten des Nutzers)**

Über `/settings` (Browser) oder curl Broker-Host/Port/User/Passwort + Gerätename eintragen, speichern. Beispiel (Platzhalter durch echte Werte ersetzen):
```bash
curl -s -m3 -X POST http://<BRIDGE-IP>/api/config -H 'Content-Type: application/json' \
 -d '{"deviceName":"MM440 Werkstatt","mqttHost":"<BROKER-IP>","mqttPort":1883,"mqttUser":"<USER>","mqttPass":"<PASS>"}'
sleep 12
```

- [ ] **Step 2: HA-Discovery prüfen**

In Home Assistant unter Einstellungen → Geräte: Gerät **„MM440 Werkstatt"** muss automatisch erscheinen (Entitäten Istfrequenz, Störung, Netzschütz, Motor, Sollfrequenz, Quittieren). Alternativ MQTT-Topics beobachten:
```bash
# auf einem Host mit mosquitto-clients, gegen den Broker:
# mosquitto_sub -h <BROKER-IP> -t 'homeassistant/#' -v | grep <sanitisierter-name>
```
Expected: Discovery-Configs veröffentlicht, Gerät in HA sichtbar, Werte aktualisieren sich.

- [ ] **Step 3: Passwort-write-only bestätigen**
```bash
curl -s -m3 http://<BRIDGE-IP>/api/config | grep -o 'mqttPass[A-Za-z]*'
```
Expected: nur `mqttPassSet` (kein `mqttPass`-Klartext). Erneutes Speichern mit leerem Passwortfeld lässt MQTT weiter verbunden (altes Passwort behalten).

- [ ] **Step 4: Doku aktualisieren**

`include/config.example.h`: Kopfkommentar bereits vorhanden (Defaults-Hinweis) — prüfen/ergänzen.
`CLAUDE.md`: unter „Stand" Absatz ergänzen:
```
## Stand 2026-07-11: Runtime-Konfiguration
Settings-Seite unter /settings + NVS (config_store). WLAN/MQTT/USS/
Gerätename zur Laufzeit änderbar (Speichern -> Reboot). config.h nur noch
Factory-Defaults; config.h in .gitignore, Vorlage config.example.h.
HA-Discovery folgt dem Gerätenamen.
```

- [ ] **Step 5: Commit + Branch mergen**
```bash
git add include/config.example.h CLAUDE.md
git commit -m "docs: Runtime-Konfiguration dokumentiert

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
git checkout master
git merge --no-ff feature/runtime-config -m "Merge: Runtime-Konfiguration (NVS + Settings-Seite)"
```

---

## Self-Review (Autor)

- **Spec-Abdeckung:** Config-Struct/NVS (T1) ✓, GET (T2), Antrieb (T3), WLAN/MQTT (T4), POST+Validierung+Reboot (T5), Settings-Seite+Link+Factory-Button (T6), Factory-Reset-Route (T7), E2E+HA+Doku (T8). Gerätename→HA-Name+Hostname+Base (T1 Sanitizer, T4/T6) ✓. Passwörter write-only (T2 GET, T5 POST) ✓. AP-Fallback bleibt (T4) ✓.
- **Platzhalter:** keine offenen TODO/TBD; Broker-Daten in T8 bewusst als Nutzer-Eingabe markiert.
- **Typkonsistenz:** `configGet/Load/Save/FactoryReset/Hostname/MqttBase`, `begin(const Config&)`, `webBegin/mqttBegin(…, const Config&)` durchgängig gleich benannt. `rebootAt` in T5 definiert, in T7 genutzt.
