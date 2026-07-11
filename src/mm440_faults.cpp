#include "mm440_faults.h"

struct CodeText { uint16_t code; const char* de; const char* en; };

static const CodeText FAULTS[] = {
  {1,"Überstrom","Overcurrent"},
  {2,"Überspannung","Overvoltage"},
  {3,"Unterspannung","Undervoltage"},
  {4,"Umrichter-Übertemperatur","Inverter over-temperature"},
  {5,"Umrichter I²t","Inverter I²t"},
  {11,"Motor-Übertemperatur","Motor over-temperature"},
  {12,"Umrichtertemp Signalverlust","Inverter temp signal lost"},
  {15,"Motortemp Signalverlust","Motor temp signal lost"},
  {20,"Netzphasenausfall","Mains phase loss"},
  {21,"Erdschluss","Earth fault"},
  {22,"Leistungsteil-Störung (HW)","Power stack fault (HW)"},
  {23,"Ausgangsfehler","Output fault"},
  {30,"Lüfter defekt","Fan failed"},
  {35,"Auto-Wiederanlauf","Auto restart"},
  {41,"Motordaten-Identifikation","Motor data identification"},
  {42,"Drehzahlregler-Optimierung","Speed control optimization"},
  {51,"Parameter-EEPROM","Parameter EEPROM"},
  {52,"Leistungsteil (Lesefehler)","Power stack (read error)"},
  {60,"Asic-Timeout","ASIC timeout"},
  {70,"CB-Sollwert (Comm-Board)","CB setpoint (comm board)"},
  {71,"USS BOP-Link Telegrammausfall","USS BOP-link telegram loss"},
  {72,"USS COM-Link Telegrammausfall","USS COM-link telegram loss"},
  {80,"ADC Signalverlust","ADC signal lost"},
  {85,"Externe Störung","External fault"},
  {101,"Stapelüberlauf","Stack overflow"},
  {221,"PID-Rückführung < min","PID feedback < min"},
  {222,"PID-Rückführung > max","PID feedback > max"},
  {450,"BIST-Diagnose","BIST diagnostics"},
};
static const CodeText WARNS[] = {
  {501,"Strombegrenzung","Current limit"},
  {502,"Überspannungsgrenze","Overvoltage limit"},
  {503,"Unterspannungsgrenze","Undervoltage limit"},
  {504,"Umrichter-Übertemperatur","Inverter over-temperature"},
  {505,"Umrichter I²t","Inverter I²t"},
  {506,"Umrichter-Lastspiel","Inverter duty cycle"},
  {511,"Motor-Übertemperatur","Motor over-temperature"},
  {512,"Motortemp Signalverlust","Motor temp signal lost"},
  {520,"Kühlkörper-Übertemperatur","Heatsink over-temperature"},
  {521,"Umgebungs-Übertemperatur","Ambient over-temperature"},
  {522,"I²C Lesetimeout","I²C read timeout"},
  {523,"Ausgangsfehler","Output fault"},
  {541,"Motordaten-Identifikation aktiv","Motor data identification active"},
  {542,"Drehzahlregler-Opt aktiv","Speed control opt active"},
  {590,"Geber Signalverlust","Encoder signal lost"},
  {600,"RTOS Overrun","RTOS overrun"},
  {700,"CB-Warnung","CB warning"},
  {710,"USS BOP-Link Komm-Fehler","USS BOP-link comm error"},
  {711,"USS COM-Link Komm-Fehler","USS COM-link comm error"},
  {910,"Vdc-max-Regler inaktiv","Vdc-max controller inactive"},
  {911,"Vdc-max-Regler aktiv","Vdc-max controller active"},
  {920,"ADC-Parameter falsch","ADC parameters wrong"},
  {922,"Keine Last am Umrichter","No load on inverter"},
  {923,"JOG links+rechts gleichzeitig","JOG left+right simultaneously"},
};

static const char* lookup(const CodeText* t, size_t n, uint16_t code, bool en) {
  for (size_t i = 0; i < n; i++)
    if (t[i].code == code) return en ? t[i].en : t[i].de;
  return nullptr;
}
const char* faultText(uint16_t code, bool en){ return lookup(FAULTS, sizeof(FAULTS)/sizeof(FAULTS[0]), code, en); }
const char* warnText (uint16_t code, bool en){ return lookup(WARNS,  sizeof(WARNS)/sizeof(WARNS[0]),  code, en); }

String faultLabel(uint16_t code, bool en) {
  const char* t = faultText(code, en);
  if (t) return String(t);
  char b[8]; snprintf(b, sizeof(b), "F%04u", code); return String(b);
}
String warnLabel(uint16_t code, bool en) {
  const char* t = warnText(code, en);
  if (t) return String(t);
  char b[8]; snprintf(b, sizeof(b), "A%04u", code); return String(b);
}
