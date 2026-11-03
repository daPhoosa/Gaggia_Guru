#include <string.h>
#include <unity.h>

#include "machine_config.h"
#include "sensor_math.h"

using namespace Sensors;

void setUp() {}
void tearDown() {}

// Build a MAX31855 frame from temperatures (no fault bits).
static uint32_t frame(float tcC, float cjC) {
  const uint32_t tc = static_cast<uint32_t>(static_cast<int32_t>(tcC / 0.25f)) & 0x3FFF;
  const uint32_t cj = static_cast<uint32_t>(static_cast<int32_t>(cjC / 0.0625f)) & 0x0FFF;
  return (tc << 18) | (cj << 4);
}

// Pin millivolts for a sensor output voltage (through the divider).
static float pinMvForSensorV(float v) { return v * 1000.0f * PRESSURE_DIVIDER_RATIO; }

void test_tc_decodes_temperatures() {
  TcReading r = decodeMax31855(frame(107.25f, 24.5f));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_NULL(r.error);
  TEST_ASSERT_EQUAL_FLOAT(107.25f, r.tempC);
  TEST_ASSERT_EQUAL_FLOAT(24.5f, r.coldJunctionC);
}

void test_tc_negative_values_sign_extended() {
  TcReading r = decodeMax31855(frame(-10.0f, -5.0f));
  TEST_ASSERT_EQUAL_FLOAT(-10.0f, r.tempC);
  TEST_ASSERT_EQUAL_FLOAT(-5.0f, r.coldJunctionC);
  TEST_ASSERT_FALSE(r.ok);  // below SAFETY_MIN_VALID_TEMP_C
  TEST_ASSERT_EQUAL_STRING("thermocouple reading implausible", r.error);
}

void test_tc_above_valid_range_rejected() {
  TcReading r = decodeMax31855(frame(SAFETY_MAX_VALID_TEMP_C + 1, 25));
  TEST_ASSERT_FALSE(r.ok);
  TEST_ASSERT_EQUAL_STRING("thermocouple reading implausible", r.error);
}

void test_tc_bus_stuck() {
  TEST_ASSERT_EQUAL_STRING("thermocouple chip not responding", decodeMax31855(0).error);
  TEST_ASSERT_EQUAL_STRING("thermocouple chip not responding", decodeMax31855(0xFFFFFFFFu).error);
  TEST_ASSERT_FALSE(decodeMax31855(0).ok);
}

void test_tc_fault_bits() {
  const uint32_t base = frame(100, 25) | (1u << 16);
  TEST_ASSERT_EQUAL_STRING("thermocouple open circuit", decodeMax31855(base | 0x1).error);
  TEST_ASSERT_EQUAL_STRING("thermocouple shorted to GND", decodeMax31855(base | 0x2).error);
  TEST_ASSERT_EQUAL_STRING("thermocouple shorted to VCC", decodeMax31855(base | 0x4).error);
  TEST_ASSERT_EQUAL_STRING("thermocouple fault", decodeMax31855(base).error);
  TEST_ASSERT_FALSE(decodeMax31855(base | 0x1).ok);
}

void test_pressure_scaling() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, pressureBarFromPinMv(pinMvForSensorV(0.5f)));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, pressureBarFromPinMv(pinMvForSensorV(2.5f)));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, pressureBarFromPinMv(pinMvForSensorV(4.5f)));
}

void test_pressure_clamped_at_zero() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pressureBarFromPinMv(pinMvForSensorV(0.4f)));
}

void test_pressure_reading_fields() {
  PressureReading r = pressureFromPinMv(pinMvForSensorV(2.5f));
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, r.sensorV);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, r.bar);
  TEST_ASSERT_EQUAL_UINT16(1363, r.pinMv);  // 2.5 V * 12/22
}

void test_pressure_valid_range() {
  TEST_ASSERT_FALSE(pressureFromPinMv(0).ok);                                          // disconnected
  TEST_ASSERT_FALSE(pressureFromPinMv(pinMvForSensorV(PRESSURE_V_VALID_LOW - 0.02f)).ok);
  TEST_ASSERT_TRUE(pressureFromPinMv(pinMvForSensorV(PRESSURE_V_VALID_LOW + 0.02f)).ok);
  TEST_ASSERT_TRUE(pressureFromPinMv(pinMvForSensorV(PRESSURE_V_VALID_HIGH - 0.02f)).ok);
  TEST_ASSERT_FALSE(pressureFromPinMv(pinMvForSensorV(PRESSURE_V_VALID_HIGH + 0.02f)).ok);  // shorted
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tc_decodes_temperatures);
  RUN_TEST(test_tc_negative_values_sign_extended);
  RUN_TEST(test_tc_above_valid_range_rejected);
  RUN_TEST(test_tc_bus_stuck);
  RUN_TEST(test_tc_fault_bits);
  RUN_TEST(test_pressure_scaling);
  RUN_TEST(test_pressure_clamped_at_zero);
  RUN_TEST(test_pressure_reading_fields);
  RUN_TEST(test_pressure_valid_range);
  return UNITY_END();
}
