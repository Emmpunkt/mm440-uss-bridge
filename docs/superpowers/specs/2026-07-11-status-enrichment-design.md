# Design: Reichere & synchrone Zustände (HA + Web)

Datum: 2026-07-11
Projekt: MM440 USS Bridge (ESP32-C3)

## Ziel

Vier Verbesserungen an der Zustandsdarstellung:

1. **Sync-Fix:** Von außen (HA) gesetzte Kommandos im Web-UI sichtbar machen.
2. **Warnung in HA:** fehlende Warnung-Entität ergänzen.
3. **Klartext:** Störungen/Warnungen als Text (kuratierte Code-Tabelle).
4. **Messwerte:** Motorstrom, Zwischenkreisspannung, Ausgangsspannung als
   HA-Sensoren + im Web (Zwischenkreis wichtig wegen späterem Bremswiderstand
   am Schrägaufzug).

## Diagnose Item 1 (bereits verifiziert)

Kein MQTT-Problem. HA-Kommando erreicht den Antrieb korrekt (getestet:
`number.set_value 7` → `/api/status.setpoint_hz = 7.0`). Ursache: die Web-UI
`poll()` schreibt `setpoint_hz` **nicht** zurück in den Slider — nur Badge/Ist-Hz/
ZSW/Warnung/Stats werden aktualisiert. Der Sollwert-Slider ist aus Web-Sicht
„write-only".

## Kernidee (macht das Design billig)

Jedes USS-Telegramm trägt die PZD (Steuerwort/Istwert) mit — zentrale Invariante.
Ein PKW-Lesezugriff ist dasselbe Frame wie ein leerer PZD-Austausch, nur mit
gefülltem PKW-Kanal. **Zusatzwerte pollen kostet daher keine Regelrate:** der
zyklische leere `exchange()` wird durch einen rotierenden PKW-Lesezugriff ersetzt,
ZSW/HIW kommen weiterhin in jedem Telegramm zurück.

## Entscheidungen (mit Nutzer abgestimmt)

- Poll-Mechanismus: **Ansatz A** — Round-Robin-Poller in `DriveControl`
  (USS-Zugriff bleibt an einer Stelle, single-threaded).
- Klartext-Umfang: **kuratierte häufige Codes + Fallback** (je ~20-30 F/A-Codes).
- Messwerte: **Strom (r0027) + Zwischenkreis (r0026) + Ausgangsspannung (r0025)**.

## Architektur

### `mm440_faults` (neu: `include/mm440_faults.h`)

Kuratierte PROGMEM-Tabellen:
```
struct CodeText { uint16_t code; const char* text; };
const char* faultText(uint16_t code);   // nullptr wenn unbekannt
const char* warnText(uint16_t code);    // nullptr wenn unbekannt
```
Aufrufer bildet bei `nullptr` `"F%04u"` / `"A%04u"`.

### `DriveControl` (erweitert)

Neue gecachte Felder + Getter:
```
float _currentA, _dcLinkV, _outVoltV;      // aus r0027/r0026/r0025 (IEEE754)
uint16_t _faultNum, _warnNum;              // aus r0947[0]/r2110[0]
uint8_t  _pollIdx;                         // Round-Robin
float currentA() const; float dcLinkV() const; float outVoltV() const;
uint16_t faultNum() const; uint16_t warnNum() const;
```

`loop()`-Änderung: statt `_uss.exchange()` bei aktiver Kommunikation pro Zyklus
einen rotierenden PKW-Lesezugriff ausführen (`pollNext()`):
- Basisrotation: r0027 → r0026 → r0025 (Rohbits → `float` via `memcpy`).
- Zusätzlich bedingt: wenn `fault()` → r0947[0] lesen nach `_faultNum`; wenn
  `alarm()` → r2110[0] nach `_warnNum`. Bit gelöscht → Wert auf 0.
- Jeder Lesezugriff aktualisiert ZSW/HIW; die bestehende Zustandslogik
  (fault/running/READY) bleibt unverändert und läuft nach dem Lesen.
- Bei fehlender Antwort: `_commFails`-Logik wie bisher (Read zählt wie
  fehlgeschlagener Zyklus).

r-Parameter-Skalierung: r0025/r0026/r0027 sind IEEE754-Float-Doppelwörter;
`readParam` liefert `uint32_t`-Rohbits → `memcpy` in `float`.

### `net_web` — `/api/status` erweitern

Zusätzliche Felder: `current_a`, `dclink_v`, `outvolt_v`, `fault_num`,
`fault_text`, `warn_num`, `warn_text`. Texte serverseitig: bei anliegender
Störung/Warnung `faultText/warnText` bzw. Fallback-Format, sonst `""`.

### `net_web` — Web-UI

- `poll()` Sync-Fix: `setpoint_hz` in Slider `#sp` + Anzeige `#spv` schreiben,
  **nur wenn** `document.activeElement !== sp` (kein Konflikt beim Ziehen).
- Neue Statuszeilen: Motorstrom [A], Zwischenkreis [V], Ausgangsspannung [V],
  Störungstext, Warnungstext.

### `net_mqtt` — Discovery + State

`publishState()` um dieselben Felder erweitern. `publishDiscovery()` neue
Entitäten:
- `binary_sensor` „Warnung" — `value_json.alarm`, device_class `problem`.
- `sensor` „Motorstrom" — `current_a`, unit `A`, device_class `current`,
  state_class `measurement`.
- `sensor` „Zwischenkreisspannung" — `dclink_v`, unit `V`, device_class
  `voltage`, measurement.
- `sensor` „Ausgangsspannung" — `outvolt_v`, unit `V`, device_class `voltage`,
  measurement.
- `sensor` „Störungstext" — `fault_text`.
- `sensor` „Warnungstext" — `warn_text`.
Unique-IDs MAC-basiert wie bestehend (`<devId>_curr` usw.).

## Fehlerbehandlung

- PKW-Lesefehler eines Zusatzwerts: alten Cache-Wert behalten, kein Absturz;
  wiederholter Kommunikationsverlust greift über bestehende `_commFails`-Logik.
- Unbekannter Fehler-/Warncode: Fallback-Textformat, nie leer bei aktivem Bit.
- Keine Kommunikation / Netzschütz aus: Messwerte nicht aktualisiert (Anzeige
  behält letzten Wert bzw. 0 nach Reset — siehe Testschritte).

## Nicht im Scope (YAGNI)

- Konfigurierbare Poll-Rate/Registerauswahl.
- Vollständige F/A-Fehlertabelle.
- Motormoment/-temperatur (spätere Option).
- Schwellwert-/Alarmlogik für Zwischenkreis (macht HA per Automation).

## Test (Hardware + HA-MCP)

1. Build/Flash.
2. **Item 1:** `number.set_value` in HA → `/api/status.setpoint_hz` folgt; im
   Browser folgt der Slider (wenn nicht fokussiert).
3. **Item 4:** Netzschütz ein → `/api/status` zeigt `current_a` (~0 ohne Motor),
   `dclink_v` (~300 V DC), `outvolt_v` plausibel.
4. **Item 2/3:** 5 Hz ohne Motor setzt real ZSW-Bit 7 (Warnung) → `warn_num`
   > 0, `warn_text` gesetzt; Entität `binary_sensor.…_warnung` in HA `on`.
5. **HA:** neue Sensor-/Binary-Entitäten erscheinen automatisch (Discovery).

## Hinweis Branch

Stapelt auf `feature/runtime-config` (dortige Config-Änderungen sind Grundlage).
Neuer Branch `feature/status-enrichment`.
