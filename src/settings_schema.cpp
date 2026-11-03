#include "settings_schema.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "machine_config.h"

namespace SettingsSchema {
namespace {

const Field kFields[] = {
    {"coffeeTempC", offsetof(MachineSettings, coffeeTempC), LIMIT_COFFEE_TEMP_MIN, LIMIT_COFFEE_TEMP_MAX},
    {"steamTempC", offsetof(MachineSettings, steamTempC), LIMIT_STEAM_TEMP_MIN, LIMIT_STEAM_TEMP_MAX},
    {"brewPressureBar", offsetof(MachineSettings, brewPressureBar), LIMIT_PRESSURE_MIN, LIMIT_PRESSURE_MAX},
    {"tempKp", offsetof(MachineSettings, tempKp), 0, LIMIT_GAIN_MAX},
    {"tempKi", offsetof(MachineSettings, tempKi), 0, LIMIT_GAIN_MAX},
    {"tempKd", offsetof(MachineSettings, tempKd), 0, LIMIT_GAIN_MAX},
    {"pressKp", offsetof(MachineSettings, pressKp), 0, LIMIT_GAIN_MAX},
    {"pressKi", offsetof(MachineSettings, pressKi), 0, LIMIT_GAIN_MAX},
    {"pressKd", offsetof(MachineSettings, pressKd), 0, LIMIT_GAIN_MAX},
    {"graphWindowS", offsetof(MachineSettings, graphWindowS), LIMIT_GRAPH_WINDOW_MIN, LIMIT_GRAPH_WINDOW_MAX},
};
constexpr size_t kCount = sizeof(kFields) / sizeof(kFields[0]);
static_assert(kCount * sizeof(float) == sizeof(MachineSettings),
              "every MachineSettings field needs an entry in kFields");

void rangeError(const Field &f, char *err, size_t errLen) {
  if (err && errLen) snprintf(err, errLen, "%s must be between %.1f and %.1f", f.key, f.min, f.max);
}

}  // namespace

size_t count() { return kCount; }
const Field &field(size_t i) { return kFields[i < kCount ? i : 0]; }

const Field *find(const char *key) {
  if (!key) return nullptr;
  for (const Field &f : kFields)
    if (strcmp(key, f.key) == 0) return &f;
  return nullptr;
}

float get(const MachineSettings &s, const Field &f) {
  float v;
  memcpy(&v, reinterpret_cast<const uint8_t *>(&s) + f.offset, sizeof(v));
  return v;
}

void set(MachineSettings &s, const Field &f, float v) {
  memcpy(reinterpret_cast<uint8_t *>(&s) + f.offset, &v, sizeof(v));
}

MachineSettings defaults() {
  MachineSettings s;
  s.coffeeTempC = DEFAULT_COFFEE_TEMP_C;
  s.steamTempC = DEFAULT_STEAM_TEMP_C;
  s.brewPressureBar = DEFAULT_BREW_PRESSURE_BAR;
  s.tempKp = DEFAULT_TEMP_KP;
  s.tempKi = DEFAULT_TEMP_KI;
  s.tempKd = DEFAULT_TEMP_KD;
  s.pressKp = DEFAULT_PRESS_KP;
  s.pressKi = DEFAULT_PRESS_KI;
  s.pressKd = DEFAULT_PRESS_KD;
  s.graphWindowS = DEFAULT_GRAPH_WINDOW_S;
  return s;
}

bool validate(const MachineSettings &s, char *err, size_t errLen) {
  for (const Field &f : kFields) {
    const float v = get(s, f);
    if (!isfinite(v) || v < f.min || v > f.max) {
      rangeError(f, err, errLen);
      return false;
    }
  }
  return true;
}

bool setField(MachineSettings &s, const char *key, const char *text, char *err, size_t errLen) {
  const Field *f = find(key);
  if (!f) {
    if (err && errLen) snprintf(err, errLen, "unknown setting %s", key ? key : "");
    return false;
  }
  char *end = nullptr;
  const float v = text ? strtof(text, &end) : 0;
  if (!text || !*text || end == text || *end != '\0' || !isfinite(v)) {
    if (err && errLen) snprintf(err, errLen, "%s is not a number", f->key);
    return false;
  }
  if (v < f->min || v > f->max) {
    rangeError(*f, err, errLen);
    return false;
  }
  set(s, *f, v);
  return true;
}

}  // namespace SettingsSchema
