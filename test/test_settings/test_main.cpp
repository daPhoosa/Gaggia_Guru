#include <math.h>
#include <string.h>
#include <unity.h>

#include "machine_config.h"
#include "settings_schema.h"

using namespace SettingsSchema;

void setUp() {}
void tearDown() {}

static char err[128];

void test_defaults_are_valid() {
  err[0] = 0;
  TEST_ASSERT_TRUE(validate(defaults(), err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("", err);
}

void test_default_values() {
  MachineSettings d = defaults();
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_COFFEE_TEMP_C, d.coffeeTempC);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_STEAM_TEMP_C, d.steamTempC);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_BREW_PRESSURE_BAR, d.brewPressureBar);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_GRAPH_WINDOW_S, d.graphWindowS);
}

void test_every_field_has_a_sane_range() {
  for (size_t i = 0; i < count(); ++i) {
    const Field &f = field(i);
    TEST_ASSERT_TRUE_MESSAGE(f.min < f.max, f.key);
    TEST_ASSERT_NOT_NULL(find(f.key));
  }
}

void test_steam_limit_below_safety_trip() {
  // A user setpoint must never be able to reach the over-temperature fault.
  TEST_ASSERT_TRUE(find("steamTempC")->max < SAFETY_MAX_TEMP_C);
  TEST_ASSERT_TRUE(find("coffeeTempC")->max < SAFETY_MAX_TEMP_C);
}

void test_set_field_accepts_valid_text() {
  MachineSettings s = defaults();
  TEST_ASSERT_TRUE(setField(s, "coffeeTempC", "104.5", err, sizeof(err)));
  TEST_ASSERT_EQUAL_FLOAT(104.5f, s.coffeeTempC);
  TEST_ASSERT_TRUE(setField(s, "pressKd", "0", err, sizeof(err)));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.pressKd);
}

void test_set_field_rejects_bad_numbers() {
  const char *bad[] = {"", "abc", "12abc", "1.5 ", "nan", "inf", "-inf"};
  for (const char *t : bad) {
    MachineSettings s = defaults();
    TEST_ASSERT_FALSE_MESSAGE(setField(s, "coffeeTempC", t, err, sizeof(err)), t);
    TEST_ASSERT_EQUAL_STRING("coffeeTempC is not a number", err);
    TEST_ASSERT_EQUAL_FLOAT(DEFAULT_COFFEE_TEMP_C, s.coffeeTempC);  // unchanged
  }
  MachineSettings s = defaults();
  TEST_ASSERT_FALSE(setField(s, "coffeeTempC", nullptr, err, sizeof(err)));
}

void test_set_field_rejects_out_of_range() {
  MachineSettings s = defaults();
  TEST_ASSERT_FALSE(setField(s, "steamTempC", "151", err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("steamTempC must be between 120.0 and 150.0", err);
  TEST_ASSERT_FALSE(setField(s, "brewPressureBar", "0.5", err, sizeof(err)));
  TEST_ASSERT_FALSE(setField(s, "tempKp", "-1", err, sizeof(err)));
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_STEAM_TEMP_C, s.steamTempC);
  // range ends are inclusive
  TEST_ASSERT_TRUE(setField(s, "steamTempC", "150", err, sizeof(err)));
  TEST_ASSERT_TRUE(setField(s, "steamTempC", "120", err, sizeof(err)));
}

void test_set_field_rejects_unknown_key() {
  MachineSettings s = defaults();
  TEST_ASSERT_FALSE(setField(s, "boilerTemp", "100", err, sizeof(err)));
  TEST_ASSERT_EQUAL_STRING("unknown setting boilerTemp", err);
}

void test_validate_rejects_nan_and_range() {
  MachineSettings s = defaults();
  s.tempKi = NAN;
  TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
  TEST_ASSERT_NOT_NULL(strstr(err, "tempKi"));
  s = defaults();
  s.coffeeTempC = 130;
  TEST_ASSERT_FALSE(validate(s, err, sizeof(err)));
  TEST_ASSERT_NOT_NULL(strstr(err, "coffeeTempC"));
}

void test_get_set_by_field() {
  MachineSettings s = defaults();
  const Field *f = find("graphWindowS");
  set(s, *f, 120);
  TEST_ASSERT_EQUAL_FLOAT(120.0f, s.graphWindowS);
  TEST_ASSERT_EQUAL_FLOAT(120.0f, get(s, *f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_valid);
  RUN_TEST(test_default_values);
  RUN_TEST(test_every_field_has_a_sane_range);
  RUN_TEST(test_steam_limit_below_safety_trip);
  RUN_TEST(test_set_field_accepts_valid_text);
  RUN_TEST(test_set_field_rejects_bad_numbers);
  RUN_TEST(test_set_field_rejects_out_of_range);
  RUN_TEST(test_set_field_rejects_unknown_key);
  RUN_TEST(test_validate_rejects_nan_and_range);
  RUN_TEST(test_get_set_by_field);
  return UNITY_END();
}
