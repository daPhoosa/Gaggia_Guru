#pragma once
#include <Arduino.h>

// User-adjustable machine settings, persisted in NVS (namespace "savant").
// Thread-safe: the web server writes, the control task reads a copy.
struct MachineSettings {
  float coffeeTempC;
  float steamTempC;
  float brewPressureBar;
  float tempKp, tempKi, tempKd;
  float pressKp, pressKi, pressKd;
  float graphWindowS;  // dashboard graph history (display only)
  // New fields go at the END (settings.cpp migrates older saved layouts).
};

namespace Settings {

void begin();                 // load from NVS (or defaults)
MachineSettings get();        // snapshot copy
MachineSettings defaults();

// Validate and store (RAM + NVS). On failure returns false, sets `error`,
// and nothing changes.
bool set(const MachineSettings &s, String &error);
void resetToDefaults();

// Set one field by its API key (e.g. "coffeeTempC") from text. Range checks
// happen here. Returns false with `error` set on unknown key or bad value.
bool setField(MachineSettings &s, const String &key, const String &value, String &error);

// Number of fields / API key of field i (for iterating form parameters).
size_t fieldCount();
const char *fieldKey(size_t i);

// {"values":{...},"defaults":{...},"limits":{key:[min,max],...}}
String json();

}  // namespace Settings
