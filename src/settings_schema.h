#pragma once
// Settings field table, defaults and validation. Hardware-free (no Arduino
// headers) so it is unit-tested on the PC (pio test -e native).
#include <stddef.h>

#include "machine_settings.h"

namespace SettingsSchema {

struct Field {
  const char *key;  // API / form / JSON name, e.g. "coffeeTempC"
  size_t offset;    // offsetof(MachineSettings, ...)
  float min, max;   // allowed range (inclusive)
};

size_t count();
const Field &field(size_t i);
const Field *find(const char *key);  // nullptr if unknown

float get(const MachineSettings &s, const Field &f);
void set(MachineSettings &s, const Field &f, float v);

MachineSettings defaults();

// Every field finite and within its range. On failure writes a message to
// err (if given) and returns false.
bool validate(const MachineSettings &s, char *err, size_t errLen);

// Parse `text` as a number and store it in field `key` of s. Rejects unknown
// keys, empty/partial/non-finite numbers and out-of-range values (s unchanged).
bool setField(MachineSettings &s, const char *key, const char *text, char *err, size_t errLen);

}  // namespace SettingsSchema
