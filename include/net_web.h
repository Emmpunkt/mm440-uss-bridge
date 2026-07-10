#pragma once
#include "drive_control.h"

// Startet WLAN (STA, Fallback AP) und den Webserver auf Port 80.
// REST-API:
//   GET  /api/status                        -> JSON-Gesamtstatus
//   POST /api/cmd    {"cmd":"mains","on":true}
//                    {"cmd":"run","on":true}
//                    {"cmd":"setpoint","hz":25.0}
//                    {"cmd":"reverse","on":false}
//                    {"cmd":"ack"}
//                    {"cmd":"save"}          (P0971=1, RAM->EEPROM)
//   GET  /api/param?pnu=2010&idx=0          -> Parameter lesen
//   POST /api/param  {"pnu":1120,"idx":0,"value":10,"dw":false}
void webBegin(DriveControl& drive);
void webLoop();
bool wifiIsAp();
String wifiIp();
