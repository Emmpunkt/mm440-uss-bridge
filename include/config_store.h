#pragma once
#include <Arduino.h>

// Zentrale Laufzeit-Konfiguration (Single-Source-of-Truth).
// Beim Boot aus NVS geladen; Defaults kommen aus config.h.
struct Config {
  uint16_t magic;          // CONFIG_MAGIC, validiert NVS-Inhalt
  uint8_t  version;        // CONFIG_VERSION
  char     deviceName[32]; // HA-Anzeigename; Quelle für Hostname/Topic
  char     wifiSsid[33];
  char     wifiPass[64];
  char     mqttHost[64];   // leer => MQTT deaktiviert
  uint16_t mqttPort;
  char     mqttUser[33];
  char     mqttPass[64];
  uint32_t ussBaud;
  uint8_t  ussSlaveAddr;
  float    refFreqHz;
  float    setpointMinHz;
  float    setpointMaxHz;
};

void    configLoad();               // NVS -> globales Config; ungültig => Defaults
Config& configGet();                // Zugriff auf geladenes Config
bool    configSave(const Config& c);// Config -> NVS (atomar)
void    configFactoryReset();       // NVS-Namespace leeren
String  configHostname();           // aus deviceName, [a-z0-9-], Fallback mm440-bridge
String  configMqttBase();           // aus deviceName, [a-z0-9-], Fallback mm440
