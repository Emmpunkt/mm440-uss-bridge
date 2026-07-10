#pragma once
#include <Arduino.h>

// ============================================================
//  USS-Master (Telegramm mit PKW=4 Worte + PZD=2 Worte)
//
//  Telegrammaufbau: STX(0x02) | LGE | ADR | Netzdaten | BCC
//    LGE = Anzahl Bytes nach LGE (ADR + Netzdaten + BCC)
//    BCC = XOR über alle Bytes von STX bis letztes Netzdatenbyte
//  Netzdaten = PKE, IND, PWE1, PWE2 (PKW) + STW/ZSW, HSW/HIW (PZD)
//  Alle Worte big-endian (High-Byte zuerst). UART: 8E1.
//
//  WICHTIG: Jedes Telegramm enthält PZD! Auch Parameterzugriffe
//  senden daher immer das aktuell gültige Steuerwort mit, sonst
//  würde der Antrieb bei laufendem Motor gestoppt.
// ============================================================

struct UssStats {
  uint32_t txCount = 0;
  uint32_t rxOk = 0;
  uint32_t timeouts = 0;
  uint32_t bccErrors = 0;
  uint32_t frameErrors = 0;
};

// PKW-Auftragskennungen (Master -> Slave)
enum class UssTask : uint8_t {
  None          = 0,
  ReadValue     = 1,
  WriteWord     = 2,   // RAM
  WriteDword    = 3,   // RAM
  ReadArray     = 6,
  WriteArrayW   = 7,   // RAM
  WriteArrayDW  = 8,   // RAM
  // EEPROM-Varianten existieren (AK 11/13/14) — hier bewusst nicht
  // genutzt: Persistenz stattdessen über P0971=1 (siehe CLAUDE.md).
};

class UssMaster {
public:
  void begin(HardwareSerial& ser, uint32_t baud,
             int8_t txPin, int8_t rxPin, int8_t dePin,
             uint8_t slaveAddr);

  // Aktuelles Steuerwort/Sollwert setzen (wird in JEDEM Telegramm gesendet)
  void setControl(uint16_t stw, int16_t hsw) { _stw = stw; _hsw = hsw; }
  uint16_t stw() const { return _stw; }
  int16_t  hsw() const { return _hsw; }

  // Zyklischer PZD-Austausch (PKW leer). true = gültige Antwort.
  bool exchange();

  // Parameter lesen (blockierend, mit Retries).
  // index: Subindex bei Array-Parametern, sonst 0.
  // errCode: PKW-Fehlernummer des Antriebs bei Antwort-AK 7.
  bool readParam(uint16_t pnu, uint8_t index, uint32_t& value, uint16_t& errCode);

  // Parameter schreiben (RAM). doubleWord=true bei 32-Bit-Werten.
  bool writeParam(uint16_t pnu, uint8_t index, uint32_t value,
                  bool doubleWord, uint16_t& errCode);

  // Letzte empfangene Prozessdaten
  uint16_t zsw() const { return _zsw; }
  int16_t  hiw() const { return _hiw; }
  const UssStats& stats() const { return _stats; }

private:
  bool transact(uint16_t pke, uint16_t ind, uint16_t pwe1, uint16_t pwe2,
                uint16_t& rPke, uint16_t& rInd, uint16_t& rPwe1, uint16_t& rPwe2);
  bool pkwRequest(UssTask task, uint16_t pnu, uint8_t index,
                  uint32_t txValue, bool doubleWord,
                  uint32_t& rxValue, uint16_t& errCode);

  HardwareSerial* _ser = nullptr;
  int8_t _dePin = -1;
  uint8_t _addr = 0;
  uint32_t _charTimeUs = 0;

  uint16_t _stw = 0;
  int16_t  _hsw = 0;
  uint16_t _zsw = 0;
  int16_t  _hiw = 0;
  UssStats _stats;
};
