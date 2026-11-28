#include "settings.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stddef.h>

#include "machine_config.h"
#include "settings_schema.h"

namespace {

constexpr uint16_t kVersion = 6;  // bump when MachineSettings layout changes

struct Stored {
  uint16_t version;
  MachineSettings s;
};

// Older layouts: the first N floats of MachineSettings (fields are only ever
// appended). v1 = 9 (no graphWindowS), v2 = 10 (no graph °C axis),
// v3 = 12 (no graph bar axis), v4 = 14 (no brewTempOffsetC), v5 = 15 (no readyBandC).
template <size_t N>
struct StoredOld {
  uint16_t version;
  float f[N];
};
using StoredV1 = StoredOld<9>;
using StoredV2 = StoredOld<10>;
using StoredV3 = StoredOld<12>;
using StoredV4 = StoredOld<14>;
using StoredV5 = StoredOld<15>;
static_assert(offsetof(MachineSettings, graphWindowS) == sizeof(StoredV1::f),
              "v1 migration assumes new fields were appended");
static_assert(offsetof(MachineSettings, graphTempMinC) == sizeof(StoredV2::f),
              "v2 migration assumes new fields were appended");
static_assert(offsetof(MachineSettings, graphPressMinBar) == sizeof(StoredV3::f),
              "v3 migration assumes new fields were appended");
static_assert(offsetof(MachineSettings, brewTempOffsetC) == sizeof(StoredV4::f),
              "v4 migration assumes new fields were appended");
static_assert(offsetof(MachineSettings, readyBandC) == sizeof(StoredV5::f),
              "v5 migration assumes new fields were appended");

// Load an older layout into st (new fields get their defaults). False if the
// blob isn't that version.
template <size_t N>
bool loadOld(Preferences &p, uint16_t version, Stored &st) {
  StoredOld<N> old;
  if (p.getBytes("settings", &old, sizeof(old)) != sizeof(old) || old.version != version) return false;
  st.version = kVersion;
  st.s = SettingsSchema::defaults();
  memcpy(&st.s, old.f, sizeof(old.f));
  return true;
}

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
  Preferences p;
  p.begin("guru", true);
  const size_t len = p.getBytesLength("settings");
  size_t n = 0;
  int migratedFrom = 0;
  if (len == sizeof(st)) {
    n = p.getBytes("settings", &st, sizeof(st));
  } else if (len == sizeof(StoredV5) && loadOld<15>(p, 5, st)) {
    n = sizeof(st);
    migratedFrom = 5;
  } else if (len == sizeof(StoredV4) && loadOld<14>(p, 4, st)) {
    n = sizeof(st);
    migratedFrom = 4;
  } else if (len == sizeof(StoredV3) && loadOld<12>(p, 3, st)) {
    n = sizeof(st);
    migratedFrom = 3;
  } else if (len == sizeof(StoredV2) && loadOld<10>(p, 2, st)) {
    n = sizeof(st);
    migratedFrom = 2;
  } else if (len == sizeof(StoredV1) && loadOld<9>(p, 1, st)) {
    n = sizeof(st);
    migratedFrom = 1;
  }
  p.end();
  const bool migrated = migratedFrom != 0;
  // Before v5, coffeeTempC was the thermocouple target (default 107 C). It is
  // now the water temp at the portafilter, so start again from the default.
  if (migrated && migratedFrom < 5) st.s.coffeeTempC = DEFAULT_COFFEE_TEMP_C;

  String err;
  if (n == sizeof(st) && st.version == kVersion && validate(st.s, err)) {
    current = st.s;
    if (migrated) save(current);
    if (migrated) Serial.printf("[Settings] Migrated v%d settings from NVS\n", migratedFrom);
    else Serial.println("[Settings] Loaded from NVS");
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
