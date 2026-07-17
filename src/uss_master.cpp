#include "uss_master.h"
#include "config.h"

// Netzdatenlänge: 4 PKW-Worte + 2 PZD-Worte = 12 Bytes
static constexpr uint8_t NET_LEN = 12;
static constexpr uint8_t FRAME_LEN = NET_LEN + 4; // STX+LGE+ADR+net+BCC

void UssMaster::begin(HardwareSerial& ser, uint32_t baud,
                      int8_t txPin, int8_t rxPin, int8_t dePin,
                      uint8_t slaveAddr) {
  _ser = &ser;
  _dePin = dePin;
  _addr = slaveAddr & 0x1F;

  pinMode(_dePin, OUTPUT);
  digitalWrite(_dePin, LOW);            // Idle = Empfangen

  // USS: 8 Datenbits, gerade Parität, 1 Stoppbit => 11 Bit/Zeichen
  _ser->begin(baud, SERIAL_8E1, rxPin, txPin);
  _charTimeUs = (11UL * 1000000UL) / baud;
}

// ------------------------------------------------------------
// Ein komplettes Telegramm senden + Antwort empfangen
// ------------------------------------------------------------
bool UssMaster::transact(uint16_t pke, uint16_t ind, uint16_t pwe1, uint16_t pwe2,
                         uint16_t& rPke, uint16_t& rInd,
                         uint16_t& rPwe1, uint16_t& rPwe2) {
  uint8_t f[FRAME_LEN];
  f[0] = 0x02;              // STX
  f[1] = NET_LEN + 2;       // LGE = ADR + Netzdaten + BCC
  f[2] = _addr;             // ADR
  // PKW
  f[3]  = pke >> 8;  f[4]  = pke & 0xFF;
  f[5]  = ind >> 8;  f[6]  = ind & 0xFF;
  f[7]  = pwe1 >> 8; f[8]  = pwe1 & 0xFF;
  f[9]  = pwe2 >> 8; f[10] = pwe2 & 0xFF;
  // PZD
  f[11] = _stw >> 8; f[12] = _stw & 0xFF;
  f[13] = (uint16_t)_hsw >> 8; f[14] = (uint16_t)_hsw & 0xFF;
  // BCC
  uint8_t bcc = 0;
  for (uint8_t i = 0; i < FRAME_LEN - 1; i++) bcc ^= f[i];
  f[FRAME_LEN - 1] = bcc;

  // Startpause (>= 2 Zeichenzeiten) + RX-Puffer leeren
  delayMicroseconds(_charTimeUs * 2);
  while (_ser->available()) _ser->read();

  // Senden
  digitalWrite(_dePin, HIGH);
  delayMicroseconds(10);
  _ser->write(f, FRAME_LEN);
  _ser->flush();                        // wartet bis TX-FIFO + Shift-Reg leer
  digitalWrite(_dePin, LOW);
  _stats.txCount++;

  // Antwort empfangen. Off-by-one-Schutz: es kann ein veraltetes/verspätetes
  // Frame der vorherigen Transaktion im Puffer liegen. Darum jedes Frame gegen
  // die Anfrage prüfen (Basis-PNU + Seitenbit) und Fremdframes verwerfen, bis
  // die zur Anfrage passende Antwort kommt oder die Zeit abläuft.
  uint8_t rx[FRAME_LEN];
  uint32_t t0 = millis();
  while (millis() - t0 <= USS_REPLY_TIMEOUT_MS) {
    // ein vollständiges Frame einlesen (auf STX synchronisieren)
    uint8_t got = 0;
    bool timedOut = false;
    while (got < FRAME_LEN) {
      if (millis() - t0 > USS_REPLY_TIMEOUT_MS) { timedOut = true; break; }
      if (!_ser->available()) { yield(); continue; }
      uint8_t b = _ser->read();
      if (got == 0 && b != 0x02) continue;    // auf STX synchronisieren
      rx[got++] = b;
    }
    if (timedOut) break;

    // Rahmen prüfen — fehlerhaft: verwerfen und nächstes Frame lesen
    if (rx[1] != NET_LEN + 2)   { _stats.frameErrors++; continue; }
    if ((rx[2] & 0x1F) != _addr) { _stats.frameErrors++; continue; }
    uint8_t rbcc = 0;
    for (uint8_t i = 0; i < FRAME_LEN - 1; i++) rbcc ^= rx[i];
    if (rbcc != rx[FRAME_LEN - 1]) { _stats.bccErrors++; continue; }

    uint16_t p    = (rx[3] << 8) | rx[4];
    uint16_t iRx  = (rx[5] << 8) | rx[6];
    // Gehört die Antwort zur Anfrage? Basis-PNU (11 Bit) + Seitenbit müssen passen.
    if ((p & 0x07FF) != (pke & 0x07FF) || (iRx & 0x8000) != (ind & 0x8000)) {
      _stats.frameErrors++;                   // veraltetes/fremdes Frame -> weiterlesen
      continue;
    }

    rPke  = p;
    rInd  = iRx;
    rPwe1 = (rx[7] << 8)  | rx[8];
    rPwe2 = (rx[9] << 8)  | rx[10];
    _zsw  = (rx[11] << 8) | rx[12];
    _hiw  = (int16_t)((rx[13] << 8) | rx[14]);
    _stats.rxOk++;
    return true;
  }
  _stats.timeouts++;
  return false;
}

// ------------------------------------------------------------
// Zyklischer PZD-Austausch, PKW-Kanal leer (AK = 0)
// ------------------------------------------------------------
bool UssMaster::exchange() {
  uint16_t a, b, c, d;
  return transact(0, 0, 0, 0, a, b, c, d);
}

// ------------------------------------------------------------
// PKW-Auftrag mit Retries
// ------------------------------------------------------------
bool UssMaster::pkwRequest(UssTask task, uint16_t pnu, uint8_t index,
                           uint32_t txValue, bool doubleWord,
                           uint32_t& rxValue, uint16_t& errCode) {
  errCode = 0xFFFF;

  // PNU-Adressierung nach MM4-USS-Spez (Siemens USS-Doku, PKW-Examples,
  //   verifiziert am lebenden MM440):
  //   Basis-PNU (0..1999) in PKE Bits 0..10,
  //   PNU-Page fuer 2000..3999 = Bit 15 (0x8000) im IND,
  //   Array-Index (Subindex) in IND Bits 0..7.
  //   Beispiel Spec: Read P2000 -> PKE=0x1000, IND=0x8000.
  uint16_t pnuBase = pnu;
  uint16_t pageBit = 0;
  if (pnu >= 2000) { pnuBase = pnu - 2000; pageBit = 0x8000; }

  uint16_t pke = ((uint8_t)task << 12) | (pnuBase & 0x07FF);
  uint16_t ind = pageBit | (index & 0x00FF);
  uint16_t pwe1 = doubleWord ? (uint16_t)(txValue >> 16) : 0;
  uint16_t pwe2 = (uint16_t)(txValue & 0xFFFF);

  for (uint8_t attempt = 0; attempt < USS_PKW_RETRIES; attempt++) {
    uint16_t rPke, rInd, rPwe1, rPwe2;
    if (!transact(pke, ind, pwe1, pwe2, rPke, rInd, rPwe1, rPwe2)) {
      delay(20);
      continue;
    }
    uint8_t respAK = rPke >> 12;
    switch (respAK) {
      case 1:            // Wert (Wort)
      case 4:            // Wert (Array, Wort)
        rxValue = rPwe2;
        return true;
      case 2:            // Wert (Doppelwort)
      case 5:            // Wert (Array, Doppelwort)
        rxValue = ((uint32_t)rPwe1 << 16) | rPwe2;
        return true;
      case 7:            // Auftrag nicht ausführbar -> Fehlernummer in PWE2
        errCode = rPwe2;
        return false;
      case 8:            // keine Bedienhoheit
        errCode = 0x0008;
        return false;
      case 0:            // Slave noch beschäftigt -> erneut anfragen
      default:
        delay(20);
        break;
    }
  }
  return false;
}

bool UssMaster::readParam(uint16_t pnu, uint8_t index,
                          uint32_t& value, uint16_t& errCode) {
  // Erst Array-Lesen versuchen (deckt indizierte Parameter ab),
  // bei Fehler "kein Array" schlicht als Einzelwert lesen.
  if (pkwRequest(UssTask::ReadArray, pnu, index, 0, false, value, errCode))
    return true;
  return pkwRequest(UssTask::ReadValue, pnu, index, 0, false, value, errCode);
}

bool UssMaster::writeParam(uint16_t pnu, uint8_t index, uint32_t value,
                           bool doubleWord, uint16_t& errCode) {
  uint32_t echo;
  UssTask t;
  if (index > 0) t = doubleWord ? UssTask::WriteArrayDW : UssTask::WriteArrayW;
  else           t = doubleWord ? UssTask::WriteDword   : UssTask::WriteWord;

  if (pkwRequest(t, pnu, index, value, doubleWord, echo, errCode))
    return true;

  // Manche Parameter sind auch bei Index 0 als Array zu adressieren
  if (index == 0) {
    t = doubleWord ? UssTask::WriteArrayDW : UssTask::WriteArrayW;
    return pkwRequest(t, pnu, index, value, doubleWord, echo, errCode);
  }
  return false;
}
