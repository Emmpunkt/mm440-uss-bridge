#include <Arduino.h>
#include "config.h"
#include "config_store.h"
#include "drive_control.h"
#include "net_web.h"
#include "net_mqtt.h"

static DriveControl drive;

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
  drive.begin();
  webBegin(drive);
  mqttBegin(drive);

  Serial.printf("Web: http://%s  (%s)\n", wifiIp().c_str(),
                wifiIsAp() ? "AP-Modus" : "STA");
}

void loop() {
  drive.loop();     // PZD-Zyklus, Zustandsmaschine
  webLoop();        // HTTP
  mqttLoop();       // MQTT/HA
}
