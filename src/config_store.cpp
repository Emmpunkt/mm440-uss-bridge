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
  copyStr(c.language, sizeof(c.language), "de");
}

void configLoad() {
  Preferences p;
  p.begin(NVS_NS, true);              // read-only
  Config tmp;
  memset(&tmp, 0, sizeof(tmp));       // ungelesene (neue) Felder = 0 -> Migration
  size_t got = p.getBytes(NVS_KEY, &tmp, sizeof(tmp));
  p.end();
  // Akzeptiert auch ältere, kürzere Blobs (magic passt) -> keine Config verloren.
  if (got > 0 && tmp.magic == CONFIG_MAGIC) {
    g_cfg = tmp;
    if (g_cfg.language[0] != 'd' && g_cfg.language[0] != 'e')  // altes Format
      copyStr(g_cfg.language, sizeof(g_cfg.language), "de");
    g_cfg.version = CONFIG_VERSION;
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
