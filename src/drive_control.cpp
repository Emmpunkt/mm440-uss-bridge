#include "drive_control.h"
#include "config.h"
#include "mm440.h"

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

void DriveControl::begin() {
  pinMode(PIN_RELAY, OUTPUT);
  relayWrite(false);
  _uss.begin(Serial1, USS_BAUD, PIN_RS485_TX, PIN_RS485_RX,
             PIN_RS485_DE, USS_SLAVE_ADDR);
  _uss.setControl(STW::READY, 0);
}

bool DriveControl::running() const { return _uss.zsw() & ZSW::RUNNING; }
bool DriveControl::fault()   const { return _uss.zsw() & ZSW::FAULT; }
bool DriveControl::alarm()   const { return _uss.zsw() & ZSW::ALARM; }

float DriveControl::actualHz() const {
  return (float)_uss.hiw() * MM440_REF_FREQ_HZ / (float)USS_FULLSCALE;
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
  _setpointHz = constrain(hz, SETPOINT_MIN_HZ, SETPOINT_MAX_HZ);
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
  int16_t hsw = (int16_t)lroundf(_setpointHz / MM440_REF_FREQ_HZ * USS_FULLSCALE);
  _uss.setControl(stw, hsw);
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
