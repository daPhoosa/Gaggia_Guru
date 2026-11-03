#include "settings.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stddef.h>

#include "settings_schema.h"

namespace {

constexpr uint16_t kVersion = 2;  // bump when MachineSettings layout changes

struct Stored {
  uint16_t version;
  MachineSettings s;
};

// Version 1 layout: the first 9 floats of MachineSettings (no graphWindowS).
struct StoredV1 {
  uint16_t version;
  float f[9];
};
static_assert(offsetof(MachineSettings, graphWindowS) == sizeof(StoredV1::f),
              "v1 migration assumes new fields were appended");

MachineSettings current;
SemaphoreHandle_t mutex = nullptr;

bool validate(const MachineSettings &s, String &error) {
  char err[96];
  if (SettingsSchema::validate(s, err, sizeof(err))) return true;
  error = err;
  return false;
}

void save(const MachineSettings &s) {
  Stored st{kVersion, s};
  Preferences p;
  p.begin("guru", false);
  p.putBytes("settings", &st, sizeof(st));
  p.end();
}

}  // namespace

namespace Settings {

MachineSettings defaults() { return SettingsSchema::defaults(); }

void begin() {
  mutex = xSemaphoreCreateMutex();
  current = defaults();

  Stored st;
  StoredV1 v1;
  Preferences p;
  p.begin("guru", true);
  const size_t len = p.getBytesLength("settings");
  size_t n = 0;
  bool migrated = false;
  if (len == sizeof(st)) {
    n = p.getBytes("settings", &st, sizeof(st));
  } else if (len == sizeof(v1) && p.getBytes("settings", &v1, sizeof(v1)) == sizeof(v1) && v1.version == 1) {
    st.version = kVersion;
    st.s = defaults();                        // new fields get their defaults
    memcpy(&st.s, v1.f, sizeof(v1.f));        // old fields keep the saved values
    n = sizeof(st);
    migrated = true;
  }
  p.end();

  String err;
  if (n == sizeof(st) && st.version == kVersion && validate(st.s, err)) {
    current = st.s;
    if (migrated) save(current);
    Serial.println(migrated ? "[Settings] Migrated v1 settings from NVS" : "[Settings] Loaded from NVS");
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
  char err[96];
  if (SettingsSchema::setField(s, key.c_str(), value.c_str(), err, sizeof(err))) return true;
  error = err;
  return false;
}

size_t fieldCount() { return SettingsSchema::count(); }
const char *fieldKey(size_t i) { return i < SettingsSchema::count() ? SettingsSchema::field(i).key : nullptr; }

String json() {
  MachineSettings s = get(), d = defaults();
  JsonDocument doc;
  JsonObject v = doc["values"].to<JsonObject>();
  JsonObject df = doc["defaults"].to<JsonObject>();
  JsonObject lim = doc["limits"].to<JsonObject>();
  for (size_t i = 0; i < SettingsSchema::count(); ++i) {
    const SettingsSchema::Field &f = SettingsSchema::field(i);
    v[f.key] = SettingsSchema::get(s, f);
    df[f.key] = SettingsSchema::get(d, f);
    JsonArray a = lim[f.key].to<JsonArray>();
    a.add(f.min);
    a.add(f.max);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace Settings
