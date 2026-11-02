#include "settings.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <stddef.h>

#include "machine_config.h"

namespace {

constexpr uint16_t kVersion = 1;  // bump when MachineSettings layout changes

struct Stored {
  uint16_t version;
  MachineSettings s;
};

struct Field {
  const char *key;
  size_t offset;
  float min, max;
};

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
};
constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

float &ref(MachineSettings &s, const Field &f) {
  return *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(&s) + f.offset);
}
float val(const MachineSettings &s, const Field &f) {
  return *reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(&s) + f.offset);
}

MachineSettings current;
SemaphoreHandle_t mutex = nullptr;

bool validate(const MachineSettings &s, String &error) {
  for (const Field &f : kFields) {
    float v = val(s, f);
    if (!isfinite(v) || v < f.min || v > f.max) {
      error = String(f.key) + " must be between " + String(f.min, 1) + " and " + String(f.max, 1);
      return false;
    }
  }
  return true;
}

void save(const MachineSettings &s) {
  Stored st{kVersion, s};
  Preferences p;
  p.begin("savant", false);
  p.putBytes("settings", &st, sizeof(st));
  p.end();
}

}  // namespace

namespace Settings {

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
  return s;
}

void begin() {
  mutex = xSemaphoreCreateMutex();
  current = defaults();

  Stored st;
  Preferences p;
  p.begin("savant", true);
  size_t n = p.getBytesLength("settings") == sizeof(st) ? p.getBytes("settings", &st, sizeof(st)) : 0;
  p.end();

  String err;
  if (n == sizeof(st) && st.version == kVersion && validate(st.s, err)) {
    current = st.s;
    Serial.println("[Settings] Loaded from NVS");
  } else {
    Serial.println("[Settings] Using defaults");
  }
}

MachineSettings get() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  MachineSettings s = current;
  xSemaphoreGive(mutex);
  return s;
}

bool set(const MachineSettings &s, String &error) {
  if (!validate(s, error)) return false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  current = s;
  xSemaphoreGive(mutex);
  save(s);
  Serial.println("[Settings] Saved");
  return true;
}

void resetToDefaults() {
  String unused;
  set(defaults(), unused);
}

bool setField(MachineSettings &s, const String &key, const String &value, String &error) {
  for (const Field &f : kFields) {
    if (key != f.key) continue;
    char *end = nullptr;
    float v = strtof(value.c_str(), &end);
    if (value.isEmpty() || end == value.c_str() || *end != '\0' || !isfinite(v)) {
      error = key + " is not a number";
      return false;
    }
    if (v < f.min || v > f.max) {
      error = key + " must be between " + String(f.min, 1) + " and " + String(f.max, 1);
      return false;
    }
    ref(s, f) = v;
    return true;
  }
  error = "unknown setting " + key;
  return false;
}

size_t fieldCount() { return kFieldCount; }
const char *fieldKey(size_t i) { return i < kFieldCount ? kFields[i].key : nullptr; }

String json() {
  MachineSettings s = get(), d = defaults();
  JsonDocument doc;
  JsonObject v = doc["values"].to<JsonObject>();
  JsonObject df = doc["defaults"].to<JsonObject>();
  JsonObject lim = doc["limits"].to<JsonObject>();
  for (const Field &f : kFields) {
    v[f.key] = val(s, f);
    df[f.key] = val(d, f);
    JsonArray a = lim[f.key].to<JsonArray>();
    a.add(f.min);
    a.add(f.max);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace Settings
