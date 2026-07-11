#include "drive_control.h"
#include "config.h"
#include "mm440.h"
#include "mm440_faults.h"
#include <string.h>

const char* driveStateName(DriveState s) {
  switch (s) {
    case DriveState::MAINS_OFF: return "MAINS_OFF";
    case DriveState::BOOTING:   return "BOOTING";
    case DriveState::READY:     return "READY";
    case DriveState::RUNNING:   return "RUNNING";
    case DriveState::FAULT:     return "FAULT";
    case DriveState::COMM_LOST: return "COMM_LOST";
  }
  return "?";
}

void DriveControl::relayWrite(bool on) {
  _relayOn = on;
  digitalWrite(PIN_RELAY, (on == RELAY_ACTIVE_HIGH) ? HIGH : LOW);
}

void DriveControl::begin(const Config& c) {
  pinMode(PIN_RELAY, OUTPUT);
  relayWrite(false);
  _refFreqHz     = c.refFreqHz;
  _setpointMinHz = c.setpointMinHz;
  _setpointMaxHz = c.setpointMaxHz;
  _uss.begin(Serial1, c.ussBaud, PIN_RS485_TX, PIN_RS485_RX,
             PIN_RS485_DE, c.ussSlaveAddr);
  _uss.setControl(STW::READY, 0);
}

bool DriveControl::running() const { return _uss.zsw() & ZSW::RUNNING; }
bool DriveControl::fault()   const { return _uss.zsw() & ZSW::FAULT; }
bool DriveControl::alarm()   const { return _uss.zsw() & ZSW::ALARM; }

float DriveControl::actualHz() const {
  return (float)_uss.hiw() * _refFreqHz / (float)USS_FULLSCALE;
}

void DriveControl::mainsOn() {
  if (_relayOn) return;
  relayWrite(true);
  _bootStart = millis();
  _commFails = 0;
  _state = DriveState::BOOTING;
}

void DriveControl::mainsOff() {
  // Sicherheit: Motor erst stoppen, dann Schütz fallen lassen
  _runRequest = false;
  if (_relayOn && commOk() && running()) {
    applyControlWord();
    for (uint8_t i = 0; i < 30 && running(); i++) { // max ~3 s Auslauf abwarten
      _uss.exchange();
      delay(100);
    }
  }
  relayWrite(false);
  _state = DriveState::MAINS_OFF;
}

void DriveControl::run(bool on) {
  _runRequest = on;
  applyControlWord();
}

void DriveControl::setSetpointHz(float hz) {
  _setpointHz = constrain(hz, _setpointMinHz, _setpointMaxHz);
  applyControlWord();
}

void DriveControl::reverse(bool rev) {
  _reverse = rev;
  applyControlWord();
}

void DriveControl::ackFault() {
  _ackCycles = 3;          // Quittierbit über 3 Zyklen setzen (Flanke)
  applyControlWord();
}

void DriveControl::applyControlWord() {
  uint16_t stw = _runRequest ? STW::RUN : STW::READY;
  if (_reverse) stw |= STW::REVERSE;
  if (_ackCycles) stw |= STW::ACK_FAULT;
  int16_t hsw = (int16_t)lroundf(_setpointHz / _refFreqHz * USS_FULLSCALE);
  _uss.setControl(stw, hsw);
}

static float bitsToFloat(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }

void DriveControl::pollExtra() {
  uint32_t raw; uint16_t err;
  switch (_pollIdx) {
    case 0: if (_uss.readParam(PNU::R_CURRENT,  0, raw, err)) _currentA = bitsToFloat(raw); break;
    case 1: if (_uss.readParam(PNU::R_DCLINK,   0, raw, err)) _dcLinkV  = bitsToFloat(raw); break;
    case 2: if (_uss.readParam(PNU::R_VOLT_OUT, 0, raw, err)) _outVoltV = bitsToFloat(raw); break;
  }
  _pollIdx = (_pollIdx + 1) % 3;
  if (fault()) { if (_uss.readParam(PNU::R_FAULT, 0, raw, err)) _faultNum = raw & 0xFFFF; }
  else _faultNum = 0;
  if (alarm()) { if (_uss.readParam(PNU::R_ALARM, 0, raw, err)) _warnNum = raw & 0xFFFF; }
  else _warnNum = 0;
}

void DriveControl::loop() {
  if (!_relayOn) { _state = DriveState::MAINS_OFF; return; }

  if (_state == DriveState::BOOTING) {
    if (millis() - _bootStart < DRIVE_BOOT_MS) return;
    _state = DriveState::READY;
  }

  if (millis() - _lastPoll < USS_POLL_MS) return;
  _lastPoll = millis();

  applyControlWord();
  bool ok = _uss.exchange();

  if (ok) {
    _commFails = 0;
    if (_ackCycles) { _ackCycles--; if (!_ackCycles) applyControlWord(); }
    if (fault())        _state = DriveState::FAULT;
    else if (running()) _state = DriveState::RUNNING;
    else                _state = DriveState::READY;
    pollExtra();                       // rotierender Zusatz-Read (trägt PZD)
  } else {
    if (_commFails < 0xFFFF) _commFails++;
    if (_commFails >= COMM_LOST_AFTER) _state = DriveState::COMM_LOST;
  }
}

// PKW-Zugriff bewusst unabhaengig vom Netzschuetz-Zustand: am Pruefstand
// haengt der Antrieb dauerhaft am Netz. Die Telegramme senden weiterhin das
// aktuelle Steuerwort (READY, kein RUN) im PZD mit -> kein Motorstart.
// Bei fehlender Antwort meldet die USS-Schicht selbst den Fehlercode.
bool DriveControl::readParam(uint16_t pnu, uint8_t index,
                             uint32_t& value, uint16_t& err) {
  return _uss.readParam(pnu, index, value, err);
}

bool DriveControl::writeParam(uint16_t pnu, uint8_t index, uint32_t value,
                              bool dw, uint16_t& err) {
  return _uss.writeParam(pnu, index, value, dw, err);
}

bool DriveControl::saveToEeprom(uint16_t& err) {
  return writeParam(PNU::P_RAM2EEPROM, 0, 1, false, err);
}
