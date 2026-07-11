# Reichere & synchrone Zustände — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** HA↔Web synchron, Warnung-Entität, Klartext für Stör-/Warncodes, Messwerte (Strom/Zwischenkreis/Ausgangsspannung) in HA + Web.

**Architecture:** Round-Robin-PKW-Poller in `DriveControl` (ein Telegramm trägt PZD → keine Regelratenkosten). Kuratierte Code→Text-Tabelle. Neue Felder in `/api/status`, MQTT-State und Discovery. Web-`poll()` schreibt Sollwert zurück (Sync-Fix).

**Tech Stack:** ESP32-C3 (Arduino), WebServer, ArduinoJson, PubSubClient.

## Global Constraints

- Deutsch. Board: `pio run` / `pio run -t upload` (`~/Dokumente/T-Display-Sattelite/.venv/bin/pio`), Bridge **<BRIDGE-IP>**.
- Kein Unit-Framework → Verifikation = build+flash+`curl`/HA. Nach Flash ~10 s auf Reconnect warten.
- **Sicherheit:** Failsafe-Reihenfolge in `setup()` NICHT ändern.
- Branch: bereits auf `feature/status-enrichment` (stapelt auf runtime-config).
- r0025/r0026/r0027 sind IEEE754-Float-Doppelwörter; `readParam` liefert Rohbits → `memcpy` in float.
- PKW trägt immer PZD → Zusatz-Reads nach erfolgreichem `exchange()`, Heartbeat/Comm-Detektion bleibt über `exchange()`.

---

### Task 1: Fault-Tabelle + DriveControl-Poller

**Files:**
- Create: `include/mm440_faults.h`, `src/mm440_faults.cpp`
- Modify: `include/drive_control.h`, `src/drive_control.cpp`

**Interfaces:**
- Produces: `String faultLabel(uint16_t)`, `String warnLabel(uint16_t)`;
  `DriveControl`-Getter `float currentA()/dcLinkV()/outVoltV()`, `uint16_t faultNum()/warnNum()`.

- [ ] **Step 1: `include/mm440_faults.h`**

```cpp
#pragma once
#include <Arduino.h>

// Kuratierte MM440-Stör-/Warncodes -> Klartext.
// Unbekannt => faultLabel/warnLabel liefern Fallback "F0123"/"A0501".
const char* faultText(uint16_t code);   // nullptr wenn unbekannt
const char* warnText(uint16_t code);    // nullptr wenn unbekannt
String faultLabel(uint16_t code);       // Text oder "F%04u"
String warnLabel(uint16_t code);        // Text oder "A%04u"
```

- [ ] **Step 2: `src/mm440_faults.cpp`**

```cpp
#include "mm440_faults.h"

struct CodeText { uint16_t code; const char* text; };

static const CodeText FAULTS[] = {
  {1,"Überstrom"},{2,"Überspannung"},{3,"Unterspannung"},
  {4,"Umrichter-Übertemperatur"},{5,"Umrichter I²t"},
  {11,"Motor-Übertemperatur"},{12,"Umrichtertemp Signalverlust"},
  {15,"Motortemp Signalverlust"},{20,"Netzphasenausfall"},
  {21,"Erdschluss"},{22,"Leistungsteil-Störung (HW)"},
  {23,"Ausgangsfehler"},{30,"Lüfter defekt"},{35,"Auto-Wiederanlauf"},
  {41,"Motordaten-Identifikation"},{42,"Drehzahlregler-Optimierung"},
  {51,"Parameter-EEPROM"},{52,"Leistungsteil (Lesefehler)"},
  {60,"Asic-Timeout"},{70,"CB-Sollwert (Comm-Board)"},
  {71,"USS BOP-Link Telegrammausfall"},{72,"USS COM-Link Telegrammausfall"},
  {80,"ADC Signalverlust"},{85,"Externe Störung"},
  {101,"Stapelüberlauf"},{221,"PID-Rückführung < min"},
  {222,"PID-Rückführung > max"},{450,"BIST-Diagnose"},
};
static const CodeText WARNS[] = {
  {501,"Strombegrenzung"},{502,"Überspannungsgrenze"},
  {503,"Unterspannungsgrenze"},{504,"Umrichter-Übertemperatur"},
  {505,"Umrichter I²t"},{506,"Umrichter-Lastspiel"},
  {511,"Motor-Übertemperatur"},{512,"Motortemp Signalverlust"},
  {520,"Kühlkörper-Übertemperatur"},{521,"Umgebungs-Übertemperatur"},
  {522,"I²C Lesetimeout"},{523,"Ausgangsfehler"},
  {541,"Motordaten-Identifikation aktiv"},{542,"Drehzahlregler-Opt aktiv"},
  {590,"Geber Signalverlust"},{600,"RTOS Overrun"},{700,"CB-Warnung"},
  {710,"USS BOP-Link Komm-Fehler"},{711,"USS COM-Link Komm-Fehler"},
  {910,"Vdc-max-Regler inaktiv"},{911,"Vdc-max-Regler aktiv"},
  {920,"ADC-Parameter falsch"},{922,"Keine Last am Umrichter"},
  {923,"JOG links+rechts gleichzeitig"},
};

static const char* lookup(const CodeText* t, size_t n, uint16_t code) {
  for (size_t i = 0; i < n; i++) if (t[i].code == code) return t[i].text;
  return nullptr;
}
const char* faultText(uint16_t code){ return lookup(FAULTS, sizeof(FAULTS)/sizeof(FAULTS[0]), code); }
const char* warnText (uint16_t code){ return lookup(WARNS,  sizeof(WARNS)/sizeof(WARNS[0]),  code); }

String faultLabel(uint16_t code) {
  const char* t = faultText(code);
  if (t) return String(t);
  char b[8]; snprintf(b, sizeof(b), "F%04u", code); return String(b);
}
String warnLabel(uint16_t code) {
  const char* t = warnText(code);
  if (t) return String(t);
  char b[8]; snprintf(b, sizeof(b), "A%04u", code); return String(b);
}
```

- [ ] **Step 3: `include/drive_control.h`** — Getter + Member ergänzen.

Nach `uint16_t zsw() ...` einfügen:
```cpp
  float currentA() const { return _currentA; }
  float dcLinkV()  const { return _dcLinkV; }
  float outVoltV() const { return _outVoltV; }
  uint16_t faultNum() const { return _faultNum; }
  uint16_t warnNum()  const { return _warnNum; }
```
Bei den privaten Membern (nach `uint16_t _commFails = 0;`) ergänzen:
```cpp
  float _currentA = 0, _dcLinkV = 0, _outVoltV = 0;
  uint16_t _faultNum = 0, _warnNum = 0;
  uint8_t _pollIdx = 0;
  void pollExtra();
```

- [ ] **Step 4: `src/drive_control.cpp`** — Include + Poller + Aufruf.

Includes oben ergänzen:
```cpp
#include "mm440_faults.h"
#include <string.h>
```
Helfer + `pollExtra()` vor `DriveControl::loop()` einfügen:
```cpp
static float bitsToFloat(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }

void DriveControl::pollExtra() {
  uint32_t raw; uint16_t err;
  switch (_pollIdx) {
    case 0: if (_uss.readParam(PNU::R_CURRENT,  0, raw, err)) _currentA = bitsToFloat(raw); break;
    case 1: if (_uss.readParam(PNU::R_DCLINK,   0, raw, err)) _dcLinkV  = bitsToFloat(raw); break;
    case 2: if (_uss.readParam(PNU::R_VOLT_OUT, 0, raw, err)) _outVoltV = bitsToFloat(raw); break;
  }
  _pollIdx = (_pollIdx + 1) % 3;
  if (fault()) { if (_uss.readParam(PNU::R_FAULT, 0, raw, err)) _faultNum = raw & 0xFFFF; }
  else _faultNum = 0;
  if (alarm()) { if (_uss.readParam(PNU::R_ALARM, 0, raw, err)) _warnNum = raw & 0xFFFF; }
  else _warnNum = 0;
}
```
In `loop()` im Erfolgszweig nach der Zustandsbestimmung `pollExtra()` aufrufen:
```cpp
  if (ok) {
    _commFails = 0;
    if (_ackCycles) { _ackCycles--; if (!_ackCycles) applyControlWord(); }
    if (fault())        _state = DriveState::FAULT;
    else if (running()) _state = DriveState::RUNNING;
    else                _state = DriveState::READY;
    pollExtra();                       // rotierender Zusatz-Read (trägt PZD)
  } else {
```

- [ ] **Step 5: Bauen**

Run: `pio run`
Expected: `[SUCCESS]`.

- [ ] **Step 6: Commit**

```bash
git add include/mm440_faults.h src/mm440_faults.cpp include/drive_control.h src/drive_control.cpp
git commit -m "drive_control: PKW-Round-Robin-Poller + Fault-Klartext-Tabelle

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: /api/status erweitern

**Files:**
- Modify: `src/net_web.cpp`

**Interfaces:**
- Consumes: `DriveControl`-Getter, `faultLabel/warnLabel` (Task 1).
- Produces: `/api/status` mit `current_a,dclink_v,outvolt_v,fault_num,fault_text,warn_num,warn_text`.

- [ ] **Step 1: Include** in `src/net_web.cpp`: `#include "mm440_faults.h"`

- [ ] **Step 2: `handleStatus()` erweitern** — vor `String out; serializeJson(d,out);` ergänzen:
```cpp
  d["current_a"] = drv->currentA();
  d["dclink_v"]  = drv->dcLinkV();
  d["outvolt_v"] = drv->outVoltV();
  d["fault_num"] = drv->faultNum();
  d["warn_num"]  = drv->warnNum();
  d["fault_text"] = drv->fault() ? faultLabel(drv->faultNum()) : String("");
  d["warn_text"]  = drv->alarm() ? warnLabel(drv->warnNum())  : String("");
```

- [ ] **Step 3: Bauen + flashen**
Run: `pio run -t upload`

- [ ] **Step 4: Verifizieren — Messwerte**
```bash
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/api/status >/dev/null && break; sleep 1; done
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"mains","on":true}' >/dev/null; sleep 7
curl -s -m3 http://<BRIDGE-IP>/api/status | python3 -m json.tool
```
Expected: `current_a`~0, `dclink_v`~300, `outvolt_v` plausibel; `fault_num`0/`fault_text`"".

- [ ] **Step 5: Verifizieren — Warnung im Lauf**
```bash
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"setpoint","hz":5}' >/dev/null
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"run","on":true}' >/dev/null; sleep 4
curl -s -m3 http://<BRIDGE-IP>/api/status | python3 -c 'import sys,json;d=json.load(sys.stdin);print("alarm=%s warn_num=%s warn_text=%s"%(d["alarm"],d["warn_num"],d["warn_text"]))'
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"run","on":false}' >/dev/null
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"mains","on":false}' >/dev/null
```
Expected: bei aktiver Warnung `warn_num`>0 und `warn_text` gesetzt (z. B. „Keine Last am Umrichter").

- [ ] **Step 6: Commit**
```bash
git add src/net_web.cpp
git commit -m "net_web: Messwerte + Stör-/Warntext in /api/status

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Web-UI — Sync-Fix + neue Zeilen

**Files:**
- Modify: `src/net_web.cpp` (INDEX_HTML)

**Interfaces:**
- Consumes: `/api/status`-Felder (Task 2).

- [ ] **Step 1: Anzeigezeilen im ersten Status-Card ergänzen** — in `INDEX_HTML` nach der ZSW-Zeile (`<div class="row"><span>ZSW</span>...`) einfügen:
```html
 <div class="row"><span>Motorstrom</span><span><span id="cur">0.0</span> A</span></div>
 <div class="row"><span>Zwischenkreis</span><span><span id="dc">0</span> V</span></div>
 <div class="row"><span>Ausgangsspannung</span><span><span id="uo">0</span> V</span></div>
 <div class="row"><span id="fault" class="err"></span><span id="warntxt" class="warn"></span></div>
```

- [ ] **Step 2: `poll()` erweitern** — im `poll()`-Body (nach der `zsw`-Zeile) ergänzen:
```javascript
 document.getElementById('cur').textContent=(s.current_a||0).toFixed(2);
 document.getElementById('dc').textContent=Math.round(s.dclink_v||0);
 document.getElementById('uo').textContent=Math.round(s.outvolt_v||0);
 document.getElementById('fault').textContent=s.fault?('Störung: '+s.fault_text):'';
 document.getElementById('warntxt').textContent=s.warn?('Warnung: '+s.warn_text):(s.alarm?('Warnung: '+s.warn_text):'');
 const sp=document.getElementById('sp');
 if(document.activeElement!==sp){sp.value=s.setpoint_hz;document.getElementById('spv').textContent=s.setpoint_hz;}
```
(Hinweis: `s.warn` existiert nicht separat — die Warnung kommt über `s.alarm`. Zeile vereinfachen zu: `document.getElementById('warntxt').textContent=s.alarm?('Warnung: '+s.warn_text):'';`)

- [ ] **Step 3: Zeile aus Step 2 korrigieren** — die `warntxt`-Zeile final so:
```javascript
 document.getElementById('warntxt').textContent=s.alarm?('Warnung: '+s.warn_text):'';
```
(die doppelte Variante aus Step 2 nicht übernehmen)

- [ ] **Step 4: Alte `#alarm`-Zeile** bleibt; sie zeigt weiterhin „⚠ Warnung aktiv". Optional redundant — belassen.

- [ ] **Step 5: Bauen + flashen**
Run: `pio run -t upload`

- [ ] **Step 6: Verifizieren — Sync-Fix**
```bash
for i in $(seq 1 20); do curl -s -m2 http://<BRIDGE-IP>/api/status >/dev/null && break; sleep 1; done
curl -s -m3 -X POST http://<BRIDGE-IP>/api/cmd -H 'Content-Type: application/json' -d '{"cmd":"mains","on":true}' >/dev/null; sleep 7
```
Dann per HA-MCP `number.set_value 8` auf `number.mm440_umrichter_sollfrequenz`, danach:
```bash
curl -s -m3 http://<BRIDGE-IP>/api/status | python3 -c 'import sys,json;print("setpoint:",json.load(sys.stdin)["setpoint_hz"])'
```
Expected: `setpoint 8.0`. Im Browser (nicht fokussierter Slider) springt der Regler auf 8. Danach Sollwert 0, mains off.

- [ ] **Step 7: Commit**
```bash
git add src/net_web.cpp
git commit -m "net_web: Web-UI Sync-Fix (Sollwert) + Messwert-/Textzeilen

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: MQTT-Discovery + State

**Files:**
- Modify: `src/net_mqtt.cpp`

**Interfaces:**
- Consumes: `DriveControl`-Getter, `faultLabel/warnLabel`.
- Produces: neue HA-Entitäten + State-Felder.

- [ ] **Step 1: Include** in `src/net_mqtt.cpp`: `#include "mm440_faults.h"`

- [ ] **Step 2: `publishState()` erweitern** — vor `String out; serializeJson(d,out);`:
```cpp
  d["current_a"] = drv->currentA();
  d["dclink_v"]  = drv->dcLinkV();
  d["outvolt_v"] = drv->outVoltV();
  d["fault_text"] = drv->fault() ? faultLabel(drv->faultNum()) : String("");
  d["warn_text"]  = drv->alarm() ? warnLabel(drv->warnNum())  : String("");
```

- [ ] **Step 3: Neue Entitäten in `publishDiscovery()`** — nach dem Quittier-Button-Block einfügen:
```cpp
  // Warnung (binary)
  {
    JsonDocument d; addDevice(d);
    d["name"] = "Warnung";
    d["unique_id"] = String(devId) + "_warn";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ 'ON' if value_json.alarm else 'OFF' }}";
    d["device_class"] = "problem";
    d["availability_topic"] = topic("availability");
    pub("binary_sensor", "warn", d);
  }
  // Motorstrom
  {
    JsonDocument d; addDevice(d);
    d["name"] = "Motorstrom";
    d["unique_id"] = String(devId) + "_curr";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ value_json.current_a | round(2) }}";
    d["unit_of_measurement"] = "A";
    d["device_class"] = "current"; d["state_class"] = "measurement";
    d["availability_topic"] = topic("availability");
    pub("sensor", "curr", d);
  }
  // Zwischenkreisspannung
  {
    JsonDocument d; addDevice(d);
    d["name"] = "Zwischenkreisspannung";
    d["unique_id"] = String(devId) + "_dclink";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ value_json.dclink_v | round(0) }}";
    d["unit_of_measurement"] = "V";
    d["device_class"] = "voltage"; d["state_class"] = "measurement";
    d["availability_topic"] = topic("availability");
    pub("sensor", "dclink", d);
  }
  // Ausgangsspannung
  {
    JsonDocument d; addDevice(d);
    d["name"] = "Ausgangsspannung";
    d["unique_id"] = String(devId) + "_uout";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ value_json.outvolt_v | round(0) }}";
    d["unit_of_measurement"] = "V";
    d["device_class"] = "voltage"; d["state_class"] = "measurement";
    d["availability_topic"] = topic("availability");
    pub("sensor", "uout", d);
  }
  // Störungstext
  {
    JsonDocument d; addDevice(d);
    d["name"] = "Störungstext";
    d["unique_id"] = String(devId) + "_ftext";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ value_json.fault_text }}";
    d["availability_topic"] = topic("availability");
    pub("sensor", "ftext", d);
  }
  // Warnungstext
  {
    JsonDocument d; addDevice(d);
    d["name"] = "Warnungstext";
    d["unique_id"] = String(devId) + "_wtext";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ value_json.warn_text }}";
    d["availability_topic"] = topic("availability");
    pub("sensor", "wtext", d);
  }
```

- [ ] **Step 4: Bauen + flashen**
Run: `pio run -t upload`

- [ ] **Step 5: Verifizieren — HA-Entitäten**
Nach Reconnect: per HA-MCP `ha_search "MM440"`.
Expected: neue Entitäten `binary_sensor.…_warnung`, `sensor.…_motorstrom`,
`sensor.…_zwischenkreisspannung`, `sensor.…_ausgangsspannung`,
`sensor.…_storungstext`, `sensor.…_warnungstext`. Netzschütz ein → Sensoren
zeigen Werte; 5 Hz-Lauf → Warnung-Binary `on`, Warnungstext gesetzt.

- [ ] **Step 6: Commit**
```bash
git add src/net_mqtt.cpp
git commit -m "net_mqtt: Discovery+State für Warnung, Messwerte, Stör-/Warntext

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review (Autor)

- **Spec-Abdeckung:** Item1 (Sync) → Task 3 Step 2/6; Item2 (Warnung) → Task 4
  binary_sensor; Item3 (Klartext) → Task 1 Tabelle + Task 2/4 Texte; Item4
  (Messwerte) → Task 1 Poller + Task 2/3/4. ✓
- **Platzhalter:** keine offenen; Task 3 Step 2/3 enthält bewusst eine Korrektur
  der `warntxt`-Zeile (Step 3 ist die finale Variante).
- **Typkonsistenz:** Getter `currentA/dcLinkV/outVoltV/faultNum/warnNum`,
  `faultLabel/warnLabel`, Status-Felder `current_a/dclink_v/outvolt_v/fault_num/
  fault_text/warn_num/warn_text` überall gleich. `pollExtra()` in Header
  deklariert + in cpp definiert.
