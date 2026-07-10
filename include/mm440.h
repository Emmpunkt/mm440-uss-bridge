#pragma once
#include <stdint.h>

// ============================================================
//  MM440 Steuerwort STW1 (PZD1, Master -> Antrieb)
// ============================================================
namespace STW {
  constexpr uint16_t ON_OFF1     = 1 << 0;   // 1 = EIN, 0 = AUS1 (Rampe)
  constexpr uint16_t NO_OFF2     = 1 << 1;   // 1 = kein AUS2 (Spannungsfrei)
  constexpr uint16_t NO_OFF3     = 1 << 2;   // 1 = kein AUS3 (Schnellhalt)
  constexpr uint16_t ENABLE_OP   = 1 << 3;   // Impulsfreigabe
  constexpr uint16_t ENABLE_RFG  = 1 << 4;   // Hochlaufgeber freigeben
  constexpr uint16_t START_RFG   = 1 << 5;   // Hochlaufgeber starten
  constexpr uint16_t ENABLE_SETP = 1 << 6;   // Sollwert freigeben
  constexpr uint16_t ACK_FAULT   = 1 << 7;   // Störung quittieren (Flanke)
  constexpr uint16_t CTRL_BY_PLC = 1 << 10;  // Führung durch PLC/USS (muss 1 sein)
  constexpr uint16_t REVERSE     = 1 << 11;  // Drehrichtungsumkehr

  // Basiswort: betriebsbereit, alle Freigaben, aber AUS1
  constexpr uint16_t READY = NO_OFF2 | NO_OFF3 | ENABLE_OP | ENABLE_RFG |
                             START_RFG | ENABLE_SETP | CTRL_BY_PLC;   // 0x047E
  constexpr uint16_t RUN   = READY | ON_OFF1;                          // 0x047F
}

// ============================================================
//  MM440 Zustandswort ZSW1 (PZD1, Antrieb -> Master)
// ============================================================
namespace ZSW {
  constexpr uint16_t READY_ON     = 1 << 0;   // einschaltbereit
  constexpr uint16_t READY_OP     = 1 << 1;   // betriebsbereit
  constexpr uint16_t RUNNING      = 1 << 2;   // Betrieb freigegeben (läuft)
  constexpr uint16_t FAULT        = 1 << 3;   // Störung aktiv
  constexpr uint16_t NO_OFF2      = 1 << 4;
  constexpr uint16_t NO_OFF3      = 1 << 5;
  constexpr uint16_t ON_INHIBIT   = 1 << 6;   // Einschaltsperre
  constexpr uint16_t ALARM        = 1 << 7;   // Warnung aktiv
  constexpr uint16_t CTRL_REQ     = 1 << 9;   // Führung angefordert
  constexpr uint16_t F_REACHED    = 1 << 10;  // Sollfrequenz erreicht
  constexpr uint16_t DIR_RIGHT    = 1 << 14;  // Drehrichtung rechts
}

// ============================================================
//  Häufig genutzte Parameter (PNU)
// ============================================================
namespace PNU {
  constexpr uint16_t R_FREQ_ACT    = 21;    // r0021 Istfrequenz [Hz]
  constexpr uint16_t R_VOLT_OUT    = 25;    // r0025 Ausgangsspannung [V]
  constexpr uint16_t R_CURRENT     = 27;    // r0027 Ausgangsstrom [A]
  constexpr uint16_t R_DCLINK      = 26;    // r0026 Zwischenkreisspannung [V]
  constexpr uint16_t R_FAULT       = 947;   // r0947 Störnummer (Array)
  constexpr uint16_t R_ALARM       = 2110;  // r2110 Warnnummer (Array)
  constexpr uint16_t P_CMD_SOURCE  = 700;   // P0700 (5 = USS COM-Link)
  constexpr uint16_t P_SETP_SOURCE = 1000;  // P1000 (5 = USS COM-Link)
  constexpr uint16_t P_REF_FREQ    = 2000;  // P2000 Bezugsfrequenz
  constexpr uint16_t P_USS_BAUD    = 2010;  // P2010 (Index 0 = COM-Link)
  constexpr uint16_t P_USS_ADDR    = 2011;
  constexpr uint16_t P_RAM2EEPROM  = 971;   // P0971 = 1: RAM -> EEPROM sichern
}

// Sollwert-Skalierung: 0x4000 (16384) entspricht 100 % von P2000
constexpr int32_t USS_FULLSCALE = 16384;
