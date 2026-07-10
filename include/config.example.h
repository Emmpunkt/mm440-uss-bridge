#pragma once
// ============================================================
//  VORLAGE — nach include/config.h kopieren und ausfuellen.
//  config.h steht in .gitignore (enthaelt Zugangsdaten).
//
//  Diese Werte sind ab dem Runtime-Config-Umbau nur noch
//  Factory-Defaults; im Betrieb kommen sie aus dem NVS
//  (Settings-Seite). Siehe docs/superpowers/specs/.
// ============================================================

// ============================================================
//  Hardware — ESP32-C3 Super Mini
// ============================================================
// RS485-Modul (MAX485/MAX3485, DE+RE gebrückt):
//   DI  <- PIN_RS485_TX
//   RO  -> PIN_RS485_RX   (bei 5V-Versorgung des Moduls: Pegelwandler!)
//   DE+RE <- PIN_RS485_DE (HIGH = senden, LOW = empfangen)
//   A -> MM440 Klemme 29 (P+), B -> MM440 Klemme 30 (N-)
#define PIN_RS485_TX      21
#define PIN_RS485_RX      20
#define PIN_RS485_DE      4

// Relais für Netzschütz des Umrichters.
// KEIN Strapping-Pin verwenden (GPIO2/8/9)!
#define PIN_RELAY         5
#define RELAY_ACTIVE_HIGH true

// ============================================================
//  USS / MM440
// ============================================================
// Muss zu den Antriebsparametern passen:
//   P2010 = 8  -> 38400 Baud  (6=9600, 7=19200, 8=38400)
//   P2011 = 0  -> USS-Adresse
//   P2012 = 2  -> PZD-Länge (Worte)
//   P2013 = 4  -> PKW-Länge fest 4 Worte  (WICHTIG, Code erwartet das)
//   P2014 = 0  -> Telegrammausfallzeit im Antrieb aus (erst später aktivieren!)
//   P0700 = 5, P1000 = 5 -> Steuerung + Sollwert über USS (COM-Link)
#define USS_BAUD              38400
#define USS_SLAVE_ADDR        0
#define USS_POLL_MS           100     // Zyklus der PZD-Abfrage
#define USS_REPLY_TIMEOUT_MS  100
#define USS_PKW_RETRIES       3

// Bezugsfrequenz P2000 des Antriebs (0x4000 im Sollwert = 100 % davon)
#define MM440_REF_FREQ_HZ     50.0f
// Sollwertgrenzen für Web/MQTT-Eingaben
#define SETPOINT_MIN_HZ       0.0f
#define SETPOINT_MAX_HZ       50.0f

// Wartezeit nach Netz-Ein (Relais) bis Kommunikationsversuch
#define DRIVE_BOOT_MS         5000
// Kommunikationsverlust nach n fehlgeschlagenen Zyklen melden
#define COMM_LOST_AFTER       10

// ============================================================
//  WLAN
// ============================================================
#define WIFI_SSID   "CHANGE_ME"
#define WIFI_PASS   "CHANGE_ME"
#define HOSTNAME    "mm440-bridge"
// Fällt das WLAN aus / ist nicht konfiguriert, startet ein eigener AP:
#define AP_SSID     "MM440-Bridge"
#define AP_PASS     "mm440setup"    // min. 8 Zeichen
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ============================================================
//  MQTT / Home Assistant  (MQTT_HOST leer lassen => deaktiviert,
//  Bridge läuft dann rein über das Webinterface)
// ============================================================
#define MQTT_HOST   ""              // z.B. "192.168.1.10"
#define MQTT_PORT   1883
#define MQTT_USER   ""
#define MQTT_PASSWORD ""
#define MQTT_BASE   "mm440"         // Topic-Präfix
#define HA_DISCOVERY_PREFIX "homeassistant"
#define HA_DEVICE_NAME "MM440 Umrichter"
