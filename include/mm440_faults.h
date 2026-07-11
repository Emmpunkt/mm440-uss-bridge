#pragma once
#include <Arduino.h>

// Kuratierte MM440-Stör-/Warncodes -> Klartext.
// Unbekannt => faultLabel/warnLabel liefern Fallback "F0123"/"A0501".
const char* faultText(uint16_t code);   // nullptr wenn unbekannt
const char* warnText(uint16_t code);    // nullptr wenn unbekannt
String faultLabel(uint16_t code);       // Text oder "F%04u"
String warnLabel(uint16_t code);        // Text oder "A%04u"
