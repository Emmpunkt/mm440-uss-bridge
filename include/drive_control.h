#pragma once
#include <Arduino.h>
#include "uss_master.h"
#include "config_store.h"

// ============================================================
//  Zustandsmaschine: Netzschütz (Relais) + USS + Betriebslogik
// ============================================================

enum class DriveState : uint8_t {
  MAINS_OFF = 0,   // Relais aus, Umrichter spannungslos
  BOOTING,         // Relais an, Umrichter fährt hoch
  READY,           // Kommunikation ok, Motor steht
  RUNNING,         // Motor läuft
  FAULT,           // Antrieb meldet Störung
  COMM_LOST        // Relais an, aber keine USS-Antwort
};

const char* driveStateName(DriveState s);

class DriveControl {
public:
  void begin(const Config& c);
  void loop();                       // zyklisch aus main-loop aufrufen

  // Befehle
  void mainsOn();
  void mainsOff();                   // stoppt vorher den Motor
  void run(bool on);                 // EIN/AUS1
  void setSetpointHz(float hz);      // begrenzt auf SETPOINT_MIN/MAX
  void reverse(bool rev);
  void ackFault();

  // Status
  DriveState state() const { return _state; }
  bool  mainsRelay() const { return _relayOn; }
  bool  running() const;
  bool  fault() const;
  bool  alarm() const;
  float actualHz() const;            // aus HIW skaliert
  float setpointHz() const { return _setpointHz; }
  uint16_t zsw() const { return _uss.zsw(); }
  bool  commOk() const { return _commFails == 0 && _relayOn && _state != DriveState::BOOTING; }

  // Parameterzugriff (Durchreiche an USS, nur bei aktiver Kommunikation)
  bool readParam(uint16_t pnu, uint8_t index, uint32_t& value, uint16_t& err);
  bool writeParam(uint16_t pnu, uint8_t index, uint32_t value, bool dw, uint16_t& err);
  bool saveToEeprom(uint16_t& err);  // P0971 = 1

  const UssStats& ussStats() const { return _uss.stats(); }

private:
  void applyControlWord();
  void relayWrite(bool on);

  UssMaster _uss;
  DriveState _state = DriveState::MAINS_OFF;
  bool _relayOn = false;
  bool _runRequest = false;
  bool _reverse = false;
  float _setpointHz = 0.0f;
  float _refFreqHz = 50.0f;
  float _setpointMinHz = 0.0f;
  float _setpointMaxHz = 50.0f;
  uint8_t _ackCycles = 0;            // Quittier-Flanke über n Zyklen
  uint32_t _bootStart = 0;
  uint32_t _lastPoll = 0;
  uint16_t _commFails = 0;
};
