<p align="center">
  <img src="docs/logo.png" alt="Logo" width="96">
</p>

<h1 align="center">MM440 USS Bridge</h1>

<p align="center">
  ESP32-C3 gateway for the Siemens MICROMASTER 440 (MM440) frequency inverter —
  control, monitoring and parameter access over USS/RS485, with a standalone web
  UI and MQTT / Home Assistant discovery.
</p>

---

*English below · [Deutsch weiter unten](#deutsch)*

## English

### What it is

A small ESP32-C3 (Super Mini) board that talks to a Siemens MICROMASTER 440 over
the **USS protocol on RS485**. It drives the mains contactor relay, exchanges
process data (control word / status word) cyclically, reads and writes drive
parameters (PKW channel), and exposes everything through:

- a **standalone web interface** (no cloud, works on the LAN), and
- **MQTT with Home Assistant auto-discovery** (optional).

### Features

- Cyclic PZD exchange (control word / setpoint ↔ status word / actual value).
- Full PKW parameter access incl. parameters **≥ 2000** (page bit `0x8000`) and
  IEEE-754 float parameters (type switch in the web UI).
- State machine: `MAINS_OFF → BOOTING → READY / RUNNING / FAULT / COMM_LOST`
  with safe contactor sequencing (motor is stopped before the contactor drops).
- Measured values as sensors: **motor current** (r0027), **DC-link voltage**
  (r0026), **output voltage** (r0025) — round-robin polled (each USS telegram
  carries the PZD, so polling costs no control rate).
- **Fault / warning in plain text** (curated MM440 code table, r0947 / r2110).
- Runtime configuration stored in NVS with a **`/settings` page** — WLAN, MQTT,
  USS baud/address, reference frequency, setpoint limits and device name are
  changeable without reflashing (save → reboot).
- Cache-safe web pages (`Cache-Control: no-store`).

### Hardware

ESP32-C3 Super Mini + an RS485 transceiver module (MAX3485 at 3.3 V recommended;
with a 5 V MAX485 use a level shifter on `RO`).

| Function | Pin (C3 Super Mini) | Note |
|---|---|---|
| RS485 TX (→ DI) | GPIO21 | UART1 |
| RS485 RX (← RO) | GPIO20 | max. 3.3 V — level-shift a 5 V module! |
| RS485 DE+RE (tied) | GPIO4 | HIGH = transmit, LOW = receive |
| Contactor relay | GPIO5 | active-high; **no** strapping pin (2/8/9) |

RS485: `A → MM440 terminal 29 (P+)`, `B → terminal 30 (N−)`. Terminate both ends;
if only timeouts increase, swap A/B first.

### MM440 prerequisites (set via BOP or PKW)

| Parameter | Value | Meaning |
|---|---|---|
| P0700 | 5 | command source = USS COM-Link |
| P1000 | 5 | setpoint source = USS COM-Link |
| P2010[0] | 8 | 38400 baud (must match `USS_BAUD`) |
| P2011[0] | 0 | USS address (must match `USS_SLAVE_ADDR`) |
| P2012[0] | 2 | PZD length = 2 words |
| P2013[0] | 4 | PKW length = 4 words (**required**, code assumes this) |
| P2014[0] | 0 | telegram-off time off initially |
| P2000 | e.g. 50 | reference frequency (must match `MM440_REF_FREQ_HZ`) |

### Build & flash

Uses [PlatformIO](https://platformio.org/). Copy `include/config.example.h` to
`include/config.h` and fill in WLAN (and optionally MQTT) — `config.h` is
git-ignored and holds only the **factory defaults**; at runtime the values come
from NVS (settings page).

```bash
cp include/config.example.h include/config.h   # then edit WLAN/MQTT
pio run                # build
pio run -t upload      # flash over native USB
pio device monitor     # debug log
```

If WLAN is not reachable the device opens an access point `MM440-Bridge`
(password `mm440setup`, UI at `192.168.4.1`).

### Web interface & REST API

Open `http://<device-ip>/`. Endpoints:

- `GET /api/status` — full status JSON
- `POST /api/cmd` — `{"cmd":"mains|run|setpoint|reverse|ack|save", ...}`
- `GET /api/param?pnu=2010&idx=0` · `POST /api/param` — read/write parameters
- `GET/POST /api/config` — runtime configuration (passwords write-only)
- `POST /api/factoryreset` — clear NVS, reboot to defaults
- `/settings` — configuration page

### MQTT / Home Assistant

Set an MQTT host on the `/settings` page. The bridge connects and publishes
Home-Assistant discovery automatically; the device shows up under its configured
name with entities: actual frequency, fault, warning, mains contactor, motor,
setpoint, acknowledge, motor current, DC-link voltage, output voltage, fault
text and warning text.

### Safety

- `setup()` forces the relay and DE inactive **before anything else**.
- `mainsOff()` stops the motor first (OFF1, waits for standstill), then drops the
  contactor — no disconnect under load.
- The setpoint is hard-limited to the configured min/max.
- **The ESP is not a safety device.** Emergency-stop and guard-door interlocks
  belong in hardware ahead of the inverter, not in this firmware.

---

<a name="deutsch"></a>

## Deutsch

### Was es ist

Eine kleine ESP32-C3-Platine (Super Mini), die per **USS-Protokoll über RS485**
mit einem Siemens MICROMASTER 440 spricht. Sie schaltet das Netzschütz-Relais,
tauscht zyklisch Prozessdaten (Steuerwort/Zustandswort) aus, liest und schreibt
Antriebsparameter (PKW-Kanal) und stellt alles bereit über:

- ein **eigenständiges Webinterface** (keine Cloud, läuft im LAN) und
- **MQTT mit Home-Assistant-Auto-Discovery** (optional).

### Funktionen

- Zyklischer PZD-Austausch (Steuerwort/Sollwert ↔ Zustandswort/Istwert).
- Voller PKW-Parameterzugriff inkl. Parameter **≥ 2000** (Seitenbit `0x8000`)
  und IEEE-754-Float-Parametern (Typumschalter im Web-UI).
- Zustandsmaschine `MAINS_OFF → BOOTING → READY / RUNNING / FAULT / COMM_LOST`
  mit sicherer Schütz-Sequenz (Motor stoppt, bevor das Schütz fällt).
- Messwerte als Sensoren: **Motorstrom** (r0027), **Zwischenkreisspannung**
  (r0026), **Ausgangsspannung** (r0025) — Round-Robin gepollt (jedes
  USS-Telegramm trägt die PZD, Polling kostet also keine Regelrate).
- **Störung / Warnung im Klartext** (kuratierte MM440-Code-Tabelle, r0947 / r2110).
- Laufzeit-Konfiguration im NVS mit **`/settings`-Seite** — WLAN, MQTT,
  USS-Baud/-Adresse, Bezugsfrequenz, Sollwertgrenzen und Gerätename ohne
  Neuflashen änderbar (Speichern → Neustart).
- Cache-sichere Seiten (`Cache-Control: no-store`).

### Hardware

ESP32-C3 Super Mini + RS485-Transceiver-Modul (MAX3485 an 3,3 V empfohlen; bei
5-V-MAX485 einen Pegelwandler auf `RO`).

| Funktion | Pin (C3 Super Mini) | Hinweis |
|---|---|---|
| RS485 TX (→ DI) | GPIO21 | UART1 |
| RS485 RX (← RO) | GPIO20 | max. 3,3 V — 5-V-Modul pegelwandeln! |
| RS485 DE+RE (gebrückt) | GPIO4 | HIGH = senden, LOW = empfangen |
| Netzschütz-Relais | GPIO5 | active-high; **kein** Strapping-Pin (2/8/9) |

RS485: `A → MM440 Klemme 29 (P+)`, `B → Klemme 30 (N−)`. Beide Enden
terminieren; steigen nur Timeouts, zuerst A/B tauschen.

### MM440-Voraussetzungen (per BOP oder PKW)

| Parameter | Wert | Bedeutung |
|---|---|---|
| P0700 | 5 | Befehlsquelle = USS COM-Link |
| P1000 | 5 | Sollwertquelle = USS COM-Link |
| P2010[0] | 8 | 38400 Baud (muss zu `USS_BAUD` passen) |
| P2011[0] | 0 | USS-Adresse (muss zu `USS_SLAVE_ADDR` passen) |
| P2012[0] | 2 | PZD-Länge = 2 Worte |
| P2013[0] | 4 | PKW-Länge = 4 Worte (**Pflicht**, Code setzt das voraus) |
| P2014[0] | 0 | Telegrammausfallzeit zunächst aus |
| P2000 | z. B. 50 | Bezugsfrequenz (muss zu `MM440_REF_FREQ_HZ` passen) |

### Bauen & flashen

Nutzt [PlatformIO](https://platformio.org/). `include/config.example.h` nach
`include/config.h` kopieren und WLAN (optional MQTT) eintragen — `config.h` ist
git-ignoriert und enthält nur die **Factory-Defaults**; im Betrieb kommen die
Werte aus dem NVS (Settings-Seite).

```bash
cp include/config.example.h include/config.h   # dann WLAN/MQTT eintragen
pio run                # bauen
pio run -t upload      # über nativen USB flashen
pio device monitor     # Debug-Log
```

Ist kein WLAN erreichbar, öffnet das Gerät den Accesspoint `MM440-Bridge`
(Passwort `mm440setup`, UI unter `192.168.4.1`).

### Webinterface & REST-API

`http://<geräte-ip>/` öffnen. Endpunkte:

- `GET /api/status` — Gesamtstatus als JSON
- `POST /api/cmd` — `{"cmd":"mains|run|setpoint|reverse|ack|save", ...}`
- `GET /api/param?pnu=2010&idx=0` · `POST /api/param` — Parameter lesen/schreiben
- `GET/POST /api/config` — Laufzeit-Konfiguration (Passwörter write-only)
- `POST /api/factoryreset` — NVS löschen, Neustart auf Defaults
- `/settings` — Konfigurationsseite

### MQTT / Home Assistant

Auf der `/settings`-Seite einen MQTT-Host eintragen. Die Bridge verbindet sich
und veröffentlicht die Home-Assistant-Discovery automatisch; das Gerät erscheint
unter seinem Namen mit den Entitäten: Istfrequenz, Störung, Warnung, Netzschütz,
Motor, Sollfrequenz, Quittieren, Motorstrom, Zwischenkreisspannung,
Ausgangsspannung, Störungstext und Warnungstext.

### Sicherheit

- `setup()` setzt Relais und DE **vor allem anderen** inaktiv.
- `mainsOff()` stoppt erst den Motor (AUS1, wartet auf Stillstand), dann fällt
  das Schütz — kein Trennen unter Last.
- Der Sollwert ist hart auf die konfigurierten Grenzen begrenzt.
- **Der ESP ist kein Sicherheitsgerät.** NOT-AUS und Schutztür-Verriegelungen
  gehören in Hardware vor den Umrichter, nicht in diese Firmware.
