#pragma once
#include <Arduino.h>

// Kuratierte MM440-Stör-/Warncodes -> Klartext (DE/EN).
// en=true -> Englisch. Unbekannt => faultLabel/warnLabel liefern
// Fallback "F0123"/"A0501" (sprachneutral).
const char* faultText(uint16_t code, bool en);   // nullptr wenn unbekannt
const char* warnText(uint16_t code, bool en);     // nullptr wenn unbekannt
String faultLabel(uint16_t code, bool en);        // Text oder "F%04u"
String warnLabel(uint16_t code, bool en);         // Text oder "A%04u"
