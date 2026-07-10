# MM440 USS Bridge — ESP32-C3 Super Mini

Steuerung, Statusauslesung und Parametrierung eines Siemens MICROMASTER 440
über USS-Protokoll (RS485), plus Relaisausgang für das Netzschütz des
Umrichters. Bedienung über Webinterface (standalone) und MQTT mit
Home-Assistant-Discovery (optional).

## Arbeitskonventionen

- Kommunikation und Kommentare auf **Deutsch**.
- Antworten **knapp und technisch dicht**.
- Bei Änderungen **komplette Dateien** ausgeben, keine Teil-Diffs.
- Bei komplexen Umbauten **schrittweise mit Rückbestätigung** arbeiten.

## Hardware

| Funktion | Pin (C3 Super Mini) | Hinweis |
|---|---|---|
| RS485 TX (→ DI) | GPIO21 | UART1 via GPIO-Matrix |
| RS485 RX (← RO) | GPIO20 | **Bei 5-V-MAX485: Pegelwandler auf RO!** Besser: MAX3485 einlöten, Modul mit 3,3 V |
| RS485 DE+RE (gebrückt) | GPIO4 | HIGH=senden, LOW=empfangen. Modul hat 10k-Pull-ups → sendet beim Boot; ggf. 2,2k-Pulldown extern |
| Relais Netzschütz | GPIO5 | active-high, Modul mit eigenem Treiber. Keine Strapping-Pins (2/8/9)! |

RS485-Modul (RBS10046): 120 Ω Termination fest bestückt (R7), Fail-Safe-Bias
20k/20k (R5/R6) vorhanden. A → MM440 Klemme 29 (P+), B → Klemme 30 (N−).
Bei Kommunikationsproblemen zuerst A/B tauschen. Punkt-zu-Punkt, beide Enden
terminiert (MM440-DIP bzw. Busabschluss antriebsseitig aktivieren).

## MM440-Voraussetzungen (per BOP oder über PKW setzen)

| Parameter | Wert | Bedeutung |
|---|---|---|
| P0700 | 5 | Befehlsquelle USS COM-Link |
| P1000 | 5 | Sollwertquelle USS COM-Link |
| P2010[0] | 8 | 38400 Baud (muss zu `USS_BAUD` passen) |
| P2011[0] | 0 | USS-Adresse (muss zu `USS_SLAVE_ADDR` passen) |
| P2012[0] | 2 | PZD-Länge 2 Worte |
| P2013[0] | 4 | **PKW-Länge fest 4 Worte — Code setzt das voraus** |
| P2014[0] | 0 | Telegrammausfallzeit zunächst AUS; erst nach stabiler Inbetriebnahme z. B. 500 ms |
| P2000 | 50 Hz | Bezugsfrequenz (muss zu `MM440_REF_FREQ_HZ` passen) |

Zugriffsstufe ggf. P0003=3. Für P07xx/P1xxx-Änderungen P0010 beachten.

## Architektur

```
main.cpp          Setup (Failsafe-Pins zuerst!), Hauptschleife
uss_master.*      USS-Protokoll: Telegramm STX|LGE|ADR|PKW(4W)|PZD(2W)|BCC,
                  8E1, BCC=XOR, DE-Umschaltung, PKW-Aufträge mit Retries
mm440.h           STW/ZSW-Bits, PNU-Konstanten, Skalierung (0x4000 = P2000)
drive_control.*   Zustandsmaschine MAINS_OFF→BOOTING→READY/RUNNING/FAULT/
                  COMM_LOST; Relais-Sequenzierung (mainsOff stoppt erst den
                  Motor); Quittier-Flanke; Sollwertbegrenzung
net_web.*         WLAN STA mit AP-Fallback (MM440-Bridge/mm440setup),
                  REST-API + eingebettete HTML-Seite (PROGMEM)
net_mqtt.*        PubSubClient, HA-Discovery (sensor/binary_sensor/switch/
                  number/button), LWT auf mm440/availability. MQTT_HOST
                  leer ⇒ komplett deaktiviert, Web läuft trotzdem
```

**Zentrale Invariante:** Jedes USS-Telegramm enthält PZD. `UssMaster` hält
STW/HSW als Zustand und sendet sie auch bei Parameterzugriffen mit — sonst
würde ein PKW-Zugriff bei laufendem Motor diesen stoppen. `applyControlWord()`
vor jedem Zyklus.

**Steuerwort:** READY=0x047E (alle Freigaben, AUS1), RUN=0x047F. Bit 10
(Führung durch PLC) muss gesetzt sein, sonst ignoriert der Antrieb die PZD.
Störquittierung = Flanke auf Bit 7 (3 Zyklen gesetzt, dann gelöscht).

**PKW:** AK 1/2/3/6/7/8 implementiert (RAM). Persistenz bewusst über
P0971=1 (RAM→EEPROM, Web-Button „EEPROM" / `{"cmd":"save"}`) statt
EEPROM-Auftragskennungen — schont das EEPROM und ist eindeutig.
PNU≥2000: Basis=PNU−2000 plus Seitenbit 0x80 im IND-Lowbyte.

## Schnittstellen

REST: siehe `include/net_web.h` (Status, cmd, param GET/POST).
MQTT: siehe `include/net_mqtt.h` (Topics unter `mm440/`).
HA: Discovery automatisch; Entitäten: Istfrequenz, Störung, Netzschütz,
Motor, Sollfrequenz, Quittieren.

## Build & Flash

```bash
pio run                    # bauen
pio run -t upload          # über USB (nativer USB-CDC des C3)
pio device monitor         # Debug-Log über USB
```

Vor dem ersten Flash: `include/config.h` → WLAN-Zugangsdaten, ggf. MQTT_HOST.

## Inbetriebnahme-Reihenfolge

1. Nur ESP + RS485-Modul, MM440 zunächst über BOP parametrieren (Tabelle oben).
2. Web-UI öffnen → „Netzschütz Ein" → nach ~5 s muss Zustand READY werden
   und ZSW plausible Werte zeigen (z. B. 0x0731/0xB731 wartend).
3. Timeout-/BCC-Zähler unten auf der Seite beobachten: sollten bei 0 bleiben.
   Steigen nur Timeouts → A/B tauschen oder Baud/Adresse prüfen.
4. Kleinen Sollwert (5 Hz) setzen, Start. Erst wenn stabil: P2014 aktivieren.

## Offene Punkte / TODO für Claude Code

- [ ] **Verifizieren gegen USS-Spez/MM4-Handbuch:** PNU≥2000-Adressierung
      (Seitenbit 0x80 im IND-Lowbyte) und Response-AK-Zuordnung 4/5 für
      Array-Antworten. Implementierung in `uss_master.cpp::pkwRequest`.
- [ ] Störnummer r0947[0] und Warnnummer r2110[0] zyklisch bei FAULT/ALARM
      lesen und in Status/HA anzeigen (Klartext-Mapping F0001…).
- [ ] Weitere Istwerte via PKW-Polling (r0027 Strom, r0026 Zwischenkreis)
      als HA-Sensoren — mit niedriger Rate (z. B. 2 s), PZD hat Vorrang.
- [ ] OTA-Updates (ArduinoOTA oder ElegantOTA).
- [ ] Konfiguration (WLAN/MQTT/Baud) nach NVS (`Preferences`) + Setup-Seite
      statt Kompilierzeit-Konstanten.
- [ ] Optional: Watchdog — bei COMM_LOST im Zustand RUNNING definiertes
      Verhalten festlegen (Relais halten vs. abschalten — Anwendungsfrage!).
- [ ] Webinterface: Basic-Auth oder zumindest Hinweis, dass das Interface
      offen im LAN steht.

## Sicherheit (nicht wegoptimieren!)

- `setup()` setzt Relais-Pin und DE **vor allem anderen** auf inaktiv.
- `mainsOff()` stoppt erst den Motor (AUS1, wartet auf Stillstand, max 3 s),
  dann fällt das Schütz — kein Trennen unter Last.
- Sollwert hart auf `SETPOINT_MIN/MAX_HZ` begrenzt.
- Der ESP ist **kein** Sicherheitskreis. NOT-AUS/Schutztüren gehören in
  Hardware vor den Umrichter, nicht in diese Firmware.
