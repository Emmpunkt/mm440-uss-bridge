#include "mm440_faults.h"

struct CodeText { uint16_t code; const char* text; };

static const CodeText FAULTS[] = {
  {1,"Überstrom"},{2,"Überspannung"},{3,"Unterspannung"},
  {4,"Umrichter-Übertemperatur"},{5,"Umrichter I²t"},
  {11,"Motor-Übertemperatur"},{12,"Umrichtertemp Signalverlust"},
  {15,"Motortemp Signalverlust"},{20,"Netzphasenausfall"},
  {21,"Erdschluss"},{22,"Leistungsteil-Störung (HW)"},
  {23,"Ausgangsfehler"},{30,"Lüfter defekt"},{35,"Auto-Wiederanlauf"},
  {41,"Motordaten-Identifikation"},{42,"Drehzahlregler-Optimierung"},
  {51,"Parameter-EEPROM"},{52,"Leistungsteil (Lesefehler)"},
  {60,"Asic-Timeout"},{70,"CB-Sollwert (Comm-Board)"},
  {71,"USS BOP-Link Telegrammausfall"},{72,"USS COM-Link Telegrammausfall"},
  {80,"ADC Signalverlust"},{85,"Externe Störung"},
  {101,"Stapelüberlauf"},{221,"PID-Rückführung < min"},
  {222,"PID-Rückführung > max"},{450,"BIST-Diagnose"},
};
static const CodeText WARNS[] = {
  {501,"Strombegrenzung"},{502,"Überspannungsgrenze"},
  {503,"Unterspannungsgrenze"},{504,"Umrichter-Übertemperatur"},
  {505,"Umrichter I²t"},{506,"Umrichter-Lastspiel"},
  {511,"Motor-Übertemperatur"},{512,"Motortemp Signalverlust"},
  {520,"Kühlkörper-Übertemperatur"},{521,"Umgebungs-Übertemperatur"},
  {522,"I²C Lesetimeout"},{523,"Ausgangsfehler"},
  {541,"Motordaten-Identifikation aktiv"},{542,"Drehzahlregler-Opt aktiv"},
  {590,"Geber Signalverlust"},{600,"RTOS Overrun"},{700,"CB-Warnung"},
  {710,"USS BOP-Link Komm-Fehler"},{711,"USS COM-Link Komm-Fehler"},
  {910,"Vdc-max-Regler inaktiv"},{911,"Vdc-max-Regler aktiv"},
  {920,"ADC-Parameter falsch"},{922,"Keine Last am Umrichter"},
  {923,"JOG links+rechts gleichzeitig"},
};

static const char* lookup(const CodeText* t, size_t n, uint16_t code) {
  for (size_t i = 0; i < n; i++) if (t[i].code == code) return t[i].text;
  return nullptr;
}
const char* faultText(uint16_t code){ return lookup(FAULTS, sizeof(FAULTS)/sizeof(FAULTS[0]), code); }
const char* warnText (uint16_t code){ return lookup(WARNS,  sizeof(WARNS)/sizeof(WARNS[0]),  code); }

String faultLabel(uint16_t code) {
  const char* t = faultText(code);
  if (t) return String(t);
  char b[8]; snprintf(b, sizeof(b), "F%04u", code); return String(b);
}
String warnLabel(uint16_t code) {
  const char* t = warnText(code);
  if (t) return String(t);
  char b[8]; snprintf(b, sizeof(b), "A%04u", code); return String(b);
}
