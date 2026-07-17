#include <Arduino.h>
#include <ArduinoOTA.h>
#include "config.h"
#include "config_store.h"
#include "drive_control.h"
#include "net_web.h"
#include "net_mqtt.h"

static DriveControl drive;
static bool otaReady = false;      // OTA nur im STA-Modus aktiv

void setup() {
  // Sicherheitszustände so früh wie möglich: Relais aus, RS485 auf Empfang.
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_HIGH ? LOW : HIGH);
  pinMode(PIN_RS485_DE, OUTPUT);
  digitalWrite(PIN_RS485_DE, LOW);

  Serial.begin(115200);              // USB-CDC (Debug)
  delay(200);
  Serial.println("\nMM440 USS Bridge");

  configLoad();                      // NVS -> Config, vor drive/web/mqtt
  drive.begin(configGet());
  webBegin(drive, configGet());
  mqttBegin(drive, configGet());

  // OTA nur wenn im WLAN (STA). Hostname aus Gerätename -> auch mDNS <name>.local
  if (!wifiIsAp()) {
    ArduinoOTA.setHostname(configHostname().c_str());
    if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
    otaReady = true;
  }

  Serial.printf("Web: http://%s  (%s)  OTA:%s\n", wifiIp().c_str(),
                wifiIsAp() ? "AP-Modus" : "STA", otaReady ? "an" : "aus");
}

void loop() {
  if (otaReady) ArduinoOTA.handle();   // OTA-Updates über WLAN
  drive.loop();     // PZD-Zyklus, Zustandsmaschine
  webLoop();        // HTTP
  mqttLoop();       // MQTT/HA
}
