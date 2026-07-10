# Design: Runtime-Konfiguration (NVS + Settings-Seite)

Datum: 2026-07-11
Projekt: MM440 USS Bridge (ESP32-C3)

## Ziel

Alle heute in `config.h` fest einkompilierten Betriebsparameter (WLAN, MQTT,
USS/Antrieb, Gerätename) zur Laufzeit über eine Web-Settings-Seite änderbar
machen und im NVS (Flash) persistieren — kein Neuflashen mehr für einen anderen
Umrichter oder einen anderen MQTT-Broker. Home Assistant findet den Umrichter
danach automatisch (MQTT-Discovery ist bereits implementiert), sobald ein Broker
hinterlegt ist.

## Entscheidungen (mit Nutzer abgestimmt)

- **Umfang:** WLAN + MQTT + USS/Antrieb + Gerätename.
- **Übernahme:** Speichern → NVS schreiben → **Neustart** (~3 s), Werte gelten
  nach dem Reboot. Kein Live-Reconfigure.
- **Zugriffsschutz:** kein Login. Passwörter **write-only** — gespeicherte
  Passwörter werden nie an den Browser zurückgeschickt; leeres Feld = altes
  Passwort behalten.
- **Ablage-Ansatz:** zentrale `struct Config` als Single-Source-of-Truth
  (Ansatz A), im NVS als Blob via `Preferences`.

## Architektur

### Neues Modul `config_store` (`src/config_store.cpp`, `include/config_store.h`)

```
struct Config {
  uint16_t magic;          // Validierung: fester Wert, z.B. 0x4D34 ("M4")
  uint8_t  version;        // Struct-Version für spätere Migration
  // Identität
  char     deviceName[32]; // Anzeigename (HA-Gerätename); Default "MM440 Umrichter"
  // WLAN
  char     wifiSsid[33];
  char     wifiPass[64];
  // MQTT (mqttHost leer => MQTT deaktiviert)
  char     mqttHost[64];
  uint16_t mqttPort;
  char     mqttUser[33];
  char     mqttPass[64];
  // USS / Antrieb
  uint32_t ussBaud;
  uint8_t  ussSlaveAddr;
  float    refFreqHz;      // = MM440 P2000, 0x4000 im Sollwert = 100 %
  float    setpointMinHz;
  float    setpointMaxHz;
};
```

Funktionen:
- `Config& configGet();` — Zugriff auf das global geladene Objekt.
- `void configLoad();` — beim Boot: `Preferences.begin("mm440cfg", true)`,
  `getBytes` in ein temporäres Objekt. Wenn `magic`/`version` nicht passen oder
  kein Eintrag existiert → mit Factory-Defaults aus `config.h` füllen. Nie mit
  ungültigem NVS-Inhalt weiterarbeiten.
- `bool configSave(const Config&);` — `putBytes` in NVS (atomar). true bei Erfolg.
- `void configFactoryReset();` — NVS-Namespace `mm440cfg` leeren (`clear()`).

Abgeleitete Werte (nicht gespeichert, zur Laufzeit aus `deviceName` berechnet):
- `configHostname()` → sanitisiert (klein, nur `[a-z0-9-]`, führende/anhängende
  `-` entfernt, leer → Fallback `mm440-bridge`).
- `configMqttBase()` → identisch sanitisiert; Fallback `mm440`. Wird Topic-Wurzel
  (`<base>/state`, `<base>/cmd/#`, `<base>/availability`).

### Factory-Defaults

Die bestehenden `#define`s in `config.h` bleiben erhalten und dienen als
Default-Quelle in `configLoad()`. Kompilierzeit-Konstanten (nicht per UI
änderbar, YAGNI): AP-Fallback (`AP_SSID`/`AP_PASS`), `WIFI_CONNECT_TIMEOUT_MS`,
`USS_POLL_MS`, `USS_REPLY_TIMEOUT_MS`, `USS_PKW_RETRIES`, `DRIVE_BOOT_MS`,
`COMM_LOST_AFTER`, `HA_DISCOVERY_PREFIX`.

### Verdrahtung

`main.cpp` ruft zuerst `configLoad()`, dann `webBegin(drive, cfg)`,
`mqttBegin(drive, cfg)`, `drive.begin(cfg)`. Signaturen der drei `*Begin`/`begin`
bekommen einen `const Config&`-Parameter; sie lesen daraus statt aus den Makros:

- `drive.begin`: `_uss.begin(Serial1, cfg.ussBaud, PIN_..., cfg.ussSlaveAddr)`;
  `refFreqHz`/`setpointMin/Max` in `DriveControl` speichern (ersetzen die Makros
  in `actualHz()`, `applyControlWord()`, `setSetpointHz()`).
- `net_web`: WLAN-Connect mit `cfg.wifiSsid/wifiPass`, Hostname aus
  `configHostname()`. Leeres SSID → direkt AP-Fallback.
- `net_mqtt`: `cfg.mqttHost` leer → deaktiviert; sonst Server/Port/User/Pass aus
  cfg, Topic-Wurzel aus `configMqttBase()`, HA-Gerätename = `cfg.deviceName`.

## Settings-Seite & API

- **`GET /settings`** → eigene HTML-Seite (PROGMEM), Formular für alle Felder,
  beim Laden per `GET /api/config` vorbefüllt. Passwortfelder bleiben leer und
  zeigen als Platzhalter „•••• (gesetzt)" wenn ein Passwort hinterlegt ist.
- **`GET /api/config`** → JSON aller Werte **ohne** Passwörter; zusätzlich
  `wifiPassSet`/`mqttPassSet` (bool).
- **`POST /api/config`** → JSON-Body validieren (siehe unten). Passwortfeld
  leer = altes behalten, sonst ersetzen. Bei Erfolg: NVS schreiben, JSON
  `{"ok":true}` senden, dann nach ~500 ms `ESP.restart()` (Verzögerung, damit
  die HTTP-Antwort noch rausgeht).
- **`POST /api/factoryreset`** → NVS leeren, OK, Reboot → AP-Modus.
- Hauptseite (`INDEX_HTML`) bekommt einen Link **„⚙ Einstellungen"** nach
  `/settings`.

### Validierung (serverseitig in `handleConfigPost`)

- `mqttPort` 1–65535 (0 nur wenn Host leer)
- `ussBaud` ∈ {9600, 19200, 38400, 57600, 115200}
- `ussSlaveAddr` 0–31
- `refFreqHz` > 0 und ≤ 650
- `setpointMinHz` ≥ 0; `setpointMaxHz` > `setpointMinHz` und ≤ 650
- `deviceName` nicht leer, ≤ 31 Zeichen
- Ungültig → HTTP 400 mit Klartext-Feld `{"ok":false,"err":"..."}`, **kein**
  Speichern, **kein** Reboot.

## Fehlerbehandlung / Wiederherstellung

- Falsche WLAN-Daten sperren nicht aus: der bestehende AP-Fallback
  (`MM440-Bridge`/`mm440setup`) greift weiterhin, wenn STA nicht verbindet →
  darüber `/settings` erreichbar und korrigierbar.
- Ungültiger/leerer NVS → Factory-Defaults (Bridge bootet immer).
- NVS-Schreibfehler → `{"ok":false}`, kein Reboot.
- „Werkseinstellungen"-Button als expliziter Rettungsanker.

## Sicherheit

- Passwörter write-only: `GET /api/config` liefert sie nie; Formular sendet leere
  Felder als „unverändert".
- Kein Login (bewusst, Heim-LAN). Der bestehende Hinweis im Projekt bleibt: der
  ESP ist kein Sicherheitskreis; NOT-AUS gehört in Hardware.

## Test (mit lebender Hardware + HA des Nutzers)

1. Build + Flash.
2. `GET /api/config` liefert Werte **ohne** Passwörter, mit `*PassSet`-Flags.
3. `/settings` lädt und ist vorbefüllt; Link von der Hauptseite funktioniert.
4. USS `refFreqHz` von 50 auf einen Testwert ändern → Speichern → Reboot →
   `GET /api/config` zeigt neuen Wert; `actualHz`-Skalierung ändert sich
   entsprechend.
5. MQTT-Host (Broker des Nutzers) + Gerätename eintragen → Speichern → Reboot →
   Bridge verbindet, **HA legt das Gerät unter dem Namen automatisch an**
   (Discovery), Topics unter `<sanitisierter-name>/…`.
6. Passwort-write-only: `GET /api/config` ohne Passwörter; Speichern mit leerem
   Passwortfeld behält das alte (MQTT verbindet weiter).
7. Factory-Reset → Reboot → AP-Modus `MM440-Bridge`.

## Nicht im Scope (YAGNI)

- Konfigurierbare Poll-/Timeout-/Retry-Tuningwerte.
- Mehrere WLAN-Profile.
- HTTPS / Login / Benutzerverwaltung.
- OTA (separater TODO-Punkt).

## Hinweis Versionierung

Das Projekt ist derzeit **kein** Git-Repo, daher wird diese Spec nicht committet.
`config.h` enthält (bald nur noch als Default) Zugangsdaten und steht nicht in
`.gitignore` — vor einer späteren Versionierung berücksichtigen.
