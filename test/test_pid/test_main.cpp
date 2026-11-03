#include <unity.h>

#include "pid.h"

void setUp() {}
void tearDown() {}

static Pid make(float kp, float ki, float kd) {
  Pid p;
  p.setGains(kp, ki, kd);
  p.setOutputLimits(0, 100);
  return p;
}

void test_proportional_only() {
  Pid p = make(2, 0, 0);
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 20.0f, p.update(100, 90, 0.1f));  // 2 * 10
}

void test_output_clamped_to_limits() {
  Pid p = make(10, 0, 0);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, p.update(100, 50, 0.1f));  // 500 -> 100
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.update(100, 150, 0.1f));   // -500 -> 0
}

void test_integral_accumulates() {
  Pid p = make(0, 1, 0);
  float out = 0;
  for (int i = 0; i < 10; ++i) out = p.update(10, 5, 0.5f);  // 1 * 5 * 0.5 * 10
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 25.0f, out);
}

void test_anti_windup_recovers_quickly() {
  // Long saturation must not build a huge integral...
  Pid p = make(1, 1, 0);
  for (int i = 0; i < 1000; ++i) p.update(100, 0, 1.0f);
  // ...so once the measurement passes the setpoint the output drops at once.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.update(100, 200, 1.0f));
  // and the integral can't sit beyond the upper limit either
  Pid q = make(0, 1, 0);
  for (int i = 0; i < 1000; ++i) q.update(100, 0, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 100.0f, q.update(100, 0, 1.0f));
  TEST_ASSERT_TRUE(q.update(0, 1, 1.0f) < 100.0f);
}

void test_derivative_on_measurement_no_setpoint_kick() {
  Pid p = make(0, 0, 10);
  p.setOutputLimits(-1000, 1000);
  p.update(50, 50, 0.1f);
  // Setpoint jumps, measurement constant: no derivative kick.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.update(100, 50, 0.1f));
  // Measurement rising 1 per 0.1 s -> D = -10 * 10 = -100.
  TEST_ASSERT_FLOAT_WITHIN(1e-3, -100.0f, p.update(100, 51, 0.1f));
}

void test_first_update_has_no_derivative() {
  Pid p = make(0, 0, 10);
  p.setOutputLimits(-1000, 1000);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, p.update(0, 80, 0.1f));
}

void test_zero_dt_returns_last_output() {
  Pid p = make(2, 0, 0);
  float a = p.update(100, 90, 0.1f);
  TEST_ASSERT_EQUAL_FLOAT(a, p.update(100, 0, 0.0f));
}

void test_reset_clears_integral() {
  Pid p = make(0, 1, 0);
  for (int i = 0; i < 10; ++i) p.update(10, 0, 1.0f);
  p.reset();
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 1.0f, p.update(10, 9, 1.0f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_proportional_only);
  RUN_TEST(test_output_clamped_to_limits);
  RUN_TEST(test_integral_accumulates);
  RUN_TEST(test_anti_windup_recovers_quickly);
  RUN_TEST(test_derivative_on_measurement_no_setpoint_kick);
  RUN_TEST(test_first_update_has_no_derivative);
  RUN_TEST(test_zero_dt_returns_last_output);
  RUN_TEST(test_reset_clears_integral);
  return UNITY_END();
}
