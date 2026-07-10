#pragma once
#include "drive_control.h"
#include "config_store.h"

// MQTT-Anbindung mit Home-Assistant-Discovery.
// Deaktiviert sich selbst, wenn MQTT_HOST leer ist — die Bridge
// bleibt dann rein über das Webinterface bedienbar.
//
// Topics (MQTT_BASE = "mm440"):
//   mm440/availability            online/offline (LWT)
//   mm440/state                   JSON-Status (retained, alle POLL-Zyklen)
//   mm440/cmd/mains               ON/OFF   -> Netzschütz
//   mm440/cmd/run                 ON/OFF   -> Motor
//   mm440/cmd/setpoint            Float Hz
//   mm440/cmd/ack                 beliebige Payload -> Störung quittieren
void mqttBegin(DriveControl& drive, const Config& c);
void mqttLoop();
