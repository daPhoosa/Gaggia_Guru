#pragma once
#include <Arduino.h>

#include "machine_settings.h"

// Settings persisted in NVS (namespace "guru"). Field table and range
// checks: settings_schema.cpp.
// Thread-safe: the web server writes, the control task reads a copy.
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
