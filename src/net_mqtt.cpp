#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "net_mqtt.h"
#include "config.h"
#include "mm440_faults.h"

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static DriveControl* drv = nullptr;
static bool enabled = false;
static uint32_t lastState = 0;
static uint32_t lastReconnect = 0;
static char devId[16];

static char mqttHost[64]; static uint16_t mqttPort;
static char mqttUser[33]; static char mqttPass[64];
static String baseTopic; static String haName;
static bool langEn = false;   // Gerätesprache für Entitätsnamen/Texte
static const char* L(const char* de, const char* en) { return langEn ? en : de; }

static String topic(const char* sub) {
  return baseTopic + "/" + sub;
}

// ------------------------------------------------------------
// HA MQTT Discovery — ein Gerät, mehrere Entitäten
// ------------------------------------------------------------
static void addDevice(JsonDocument& d) {
  JsonObject dev = d["device"].to<JsonObject>();
  dev["identifiers"][0] = devId;
  dev["name"] = haName.c_str();
  dev["manufacturer"] = "Siemens/DIY";
  dev["model"] = "MM440 USS Bridge (ESP32-C3)";
}

static void publishDiscovery() {
  struct Ent { const char* comp; const char* oid; const char* name;
               void (*fill)(JsonDocument&); };

  auto pub = [](const char* comp, const char* oid, JsonDocument& d) {
    String t = String(HA_DISCOVERY_PREFIX) + "/" + comp + "/" + devId + "/" + oid + "/config";
    String out; serializeJson(d, out);
    mqtt.publish(t.c_str(), out.c_str(), true);
  };

  // Istfrequenz
  {
    JsonDocument d; addDevice(d);
    d["name"] = L("Istfrequenz","Actual frequency");
    d["unique_id"] = String(devId) + "_freq";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ value_json.actual_hz | round(1) }}";
    d["unit_of_measurement"] = "Hz";
    d["state_class"] = "measurement";
    d["availability_topic"] = topic("availability");
    pub("sensor", "freq", d);
  }
  // Störung
  {
    JsonDocument d; addDevice(d);
    d["name"] = L("Störung","Fault");
    d["unique_id"] = String(devId) + "_fault";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ 'ON' if value_json.fault else 'OFF' }}";
    d["device_class"] = "problem";
    d["availability_topic"] = topic("availability");
    pub("binary_sensor", "fault", d);
  }
  // Netzschütz
  {
    JsonDocument d; addDevice(d);
    d["name"] = L("Netzschütz","Mains contactor");
    d["unique_id"] = String(devId) + "_mains";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ 'ON' if value_json.mains else 'OFF' }}";
    d["command_topic"] = topic("cmd/mains");
    d["availability_topic"] = topic("availability");
    pub("switch", "mains", d);
  }
  // Motor Start/Stopp
  {
    JsonDocument d; addDevice(d);
    d["name"] = L("Motor","Motor");
    d["unique_id"] = String(devId) + "_run";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ 'ON' if value_json.running else 'OFF' }}";
    d["command_topic"] = topic("cmd/run");
    d["availability_topic"] = topic("availability");
    pub("switch", "run", d);
  }
  // Sollwert
  {
    JsonDocument d; addDevice(d);
    d["name"] = L("Sollfrequenz","Setpoint frequency");
    d["unique_id"] = String(devId) + "_setp";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ value_json.setpoint_hz }}";
    d["command_topic"] = topic("cmd/setpoint");
    d["min"] = SETPOINT_MIN_HZ; d["max"] = SETPOINT_MAX_HZ; d["step"] = 0.5;
    d["unit_of_measurement"] = "Hz";
    d["mode"] = "slider";
    d["availability_topic"] = topic("availability");
    pub("number", "setpoint", d);
  }
  // Quittier-Button
  {
    JsonDocument d; addDevice(d);
    d["name"] = L("Störung quittieren","Acknowledge fault");
    d["unique_id"] = String(devId) + "_ack";
    d["command_topic"] = topic("cmd/ack");
    d["availability_topic"] = topic("availability");
    pub("button", "ack", d);
  }
  // Warnung (binary)
  {
    JsonDocument d; addDevice(d);
    d["name"] = L("Warnung","Warning");
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
    d["name"] = L("Motorstrom","Motor current");
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
    d["name"] = L("Zwischenkreisspannung","DC-link voltage");
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
    d["name"] = L("Ausgangsspannung","Output voltage");
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
    d["name"] = L("Störungstext","Fault text");
    d["unique_id"] = String(devId) + "_ftext";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ value_json.fault_text }}";
    d["availability_topic"] = topic("availability");
    pub("sensor", "ftext", d);
  }
  // Warnungstext
  {
    JsonDocument d; addDevice(d);
    d["name"] = L("Warnungstext","Warning text");
    d["unique_id"] = String(devId) + "_wtext";
    d["state_topic"] = topic("state");
    d["value_template"] = "{{ value_json.warn_text }}";
    d["availability_topic"] = topic("availability");
    pub("sensor", "wtext", d);
  }
}

static void publishState() {
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
  d["fault_text"] = drv->fault() ? faultLabel(drv->faultNum(), langEn) : String("");
  d["warn_text"]  = drv->alarm() ? warnLabel(drv->warnNum(), langEn)  : String("");
  String out; serializeJson(d, out);
  mqtt.publish(topic("state").c_str(), out.c_str(), true);
}

static void onMessage(char* t, byte* payload, unsigned int len) {
  String tp(t);
  String pl; pl.reserve(len);
  for (unsigned int i = 0; i < len; i++) pl += (char)payload[i];

  if (tp == topic("cmd/mains"))    { pl == "ON" ? drv->mainsOn() : drv->mainsOff(); }
  else if (tp == topic("cmd/run")) { drv->run(pl == "ON"); }
  else if (tp == topic("cmd/setpoint")) { drv->setSetpointHz(pl.toFloat()); }
  else if (tp == topic("cmd/ack")) { drv->ackFault(); }
  publishState();
}

static void connect() {
  String will = topic("availability");
  if (mqtt.connect(devId, mqttUser, mqttPass,
                   will.c_str(), 0, true, "offline")) {
    mqtt.publish(will.c_str(), "online", true);
    mqtt.subscribe(topic("cmd/#").c_str());
    publishDiscovery();
    publishState();
  }
}

void mqttBegin(DriveControl& drive, const Config& c) {
  drv = &drive;
  if (strlen(c.mqttHost) == 0) { enabled = false; return; }
  enabled = true;
  strncpy(mqttHost, c.mqttHost, sizeof(mqttHost) - 1); mqttHost[sizeof(mqttHost)-1] = '\0';
  strncpy(mqttUser, c.mqttUser, sizeof(mqttUser) - 1); mqttUser[sizeof(mqttUser)-1] = '\0';
  strncpy(mqttPass, c.mqttPass, sizeof(mqttPass) - 1); mqttPass[sizeof(mqttPass)-1] = '\0';
  mqttPort  = c.mqttPort;
  baseTopic = configMqttBase();
  haName    = c.deviceName;
  langEn    = (strcmp(c.language, "en") == 0);
  snprintf(devId, sizeof(devId), "mm440_%06lx",
           (unsigned long)(ESP.getEfuseMac() & 0xFFFFFF));
  mqtt.setServer(mqttHost, mqttPort);
  mqtt.setBufferSize(1024);          // Discovery-Payloads > 256 Byte
  mqtt.setCallback(onMessage);
}

void mqttLoop() {
  if (!enabled || WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    if (millis() - lastReconnect > 5000) { lastReconnect = millis(); connect(); }
    return;
  }
  mqtt.loop();
  if (millis() - lastState > 1000) { lastState = millis(); publishState(); }
}
