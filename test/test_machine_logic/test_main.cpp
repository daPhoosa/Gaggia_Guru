// Machine::Logic: states, switch handling, outputs and every software safety
// fault, driven by a simulator that steps 50 ms control ticks.
#include <string.h>
#include <unity.h>

#include "machine_config.h"
#include "machine_logic.h"
#include "settings_schema.h"

using Machine::Inputs;
using Machine::Logic;
using Machine::Outputs;
using Machine::State;
using Machine::Status;

void setUp() {}
void tearDown() {}

static float pinMvForBar(float bar) {
  const float sensorV = PRESSURE_V_MIN + bar / PRESSURE_FULL_SCALE_BAR * (PRESSURE_V_MAX - PRESSURE_V_MIN);
  return sensorV * 1000.0f * PRESSURE_DIVIDER_RATIO;
}

// ---- simulator ---------------------------------------------------------------
struct Sim {
  Logic m;
  MachineSettings cfg = SettingsSchema::defaults();
  Outputs out{};

  // "hardware"
  uint32_t now = 10000;
  bool coffee = false, steam = false;
  float tempC = DEFAULT_COFFEE_TEMP_C;
  const char *tcError = nullptr;  // non-null: thermocouple reads fail with this
  float pinMv = pinMvForBar(0);
  float supplyPinMv = 0;          // > 0: ADS1115-style reading with supply measurement
  const char *pressError = nullptr;  // non-null: converter failure
  bool zeroCross = true;          // dimmer zero-cross pulses arriving
  bool inhibited = false;

  uint32_t zcCount = 0, lastZcMs = 0;
  int tcReads = 0;

  void begin() {
    lastZcMs = now;
    m.begin(now, coffee, steam, zcCount);
  }

  void step() {
    now += CONTROL_PERIOD_MS;
    if (zeroCross) {
      zcCount += CONTROL_PERIOD_MS * 120 / 1000;  // 60 Hz mains = 120 crossings/s
      lastZcMs = now;
    }
    Inputs in{};
    in.nowMs = now;
    in.coffeeSwRaw = coffee;
    in.steamSwRaw = steam;
    in.tcSampled = m.tcDue(now);
    if (in.tcSampled) {
      ++tcReads;
      in.tc = tcError ? Sensors::TcReading{false, 0, 25, tcError} : Sensors::TcReading{true, tempC, 25, nullptr};
    }
    in.pressure = pressError ? Sensors::pressureError(pressError)
                  : supplyPinMv > 0 ? Sensors::pressureFromPinMv(pinMv, supplyPinMv)
                                    : Sensors::pressureFromPinMv(pinMv);
    in.msSinceZeroCross = now - lastZcMs;
    in.zeroCrossCount = zcCount;
    in.inhibited = inhibited;
    out = m.tick(in, cfg);
  }

  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += CONTROL_PERIOD_MS) step();
  }

  // Flip the Coffee switch and wait past the debounce.
  void setCoffee(bool on) {
    coffee = on;
    run(CONTROL_PERIOD_MS * (SWITCH_DEBOUNCE_TICKS + 1));
  }
  void setSteam(bool on) {
    steam = on;
    run(CONTROL_PERIOD_MS * (SWITCH_DEBOUNCE_TICKS + 1));
  }

  const Status &st() const { return m.status(); }
};

#define ASSERT_STATE(expected, sim) \
  TEST_ASSERT_EQUAL_STRING(Machine::stateName(expected), Machine::stateName((sim).st().state))

static void assertAllLoadsOff(const Sim &s) {
  TEST_ASSERT_FALSE(s.out.heater);
  TEST_ASSERT_FALSE(s.out.valve);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
}

static void assertFaultContains(const Sim &s, const char *text) {
  ASSERT_STATE(State::Fault, s);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(s.st().fault, text), s.st().fault);
}

// ---- states --------------------------------------------------------------------
void test_starts_idle_and_heats_to_coffee_setpoint() {
  Sim s;
  s.tempC = 25;
  s.begin();
  s.step();
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_COFFEE_TEMP_C, s.st().setpointC);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, s.st().heaterDuty);  // far below setpoint
  TEST_ASSERT_TRUE(s.out.heater);
  TEST_ASSERT_FALSE(s.out.valve);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
  TEST_ASSERT_FALSE(s.out.coffeeLight);  // not ready
}

void test_ready_light_within_band() {
  Sim s;
  s.tempC = DEFAULT_COFFEE_TEMP_C - READY_BAND_C + 0.25f;
  s.begin();
  s.step();
  TEST_ASSERT_TRUE(s.out.coffeeLight);
  TEST_ASSERT_FALSE(s.out.steamLight);
  s.tempC = DEFAULT_COFFEE_TEMP_C - READY_BAND_C - 0.5f;
  s.run(TC_READ_PERIOD_MS);
  TEST_ASSERT_FALSE(s.out.coffeeLight);
}

void test_heater_off_above_setpoint() {
  Sim s;
  s.tempC = DEFAULT_COFFEE_TEMP_C + 5;
  s.begin();
  s.run(HEATER_WINDOW_MS);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.st().heaterDuty);
  TEST_ASSERT_FALSE(s.out.heater);
}

void test_heater_time_proportioning() {
  Sim s;
  s.cfg.tempKp = 10;  // 10 %/C, no I/D: 5 C below setpoint -> 50 %
  s.cfg.tempKi = 0;
  s.cfg.tempKd = 0;
  s.tempC = DEFAULT_COFFEE_TEMP_C - 5;
  s.begin();
  s.run(HEATER_WINDOW_MS);  // settle into window alignment
  int on = 0, ticks = 0;
  for (uint32_t t = 0; t < 10 * HEATER_WINDOW_MS; t += CONTROL_PERIOD_MS, ++ticks) {
    s.step();
    on += s.out.heater;
  }
  TEST_ASSERT_EQUAL_FLOAT(50.0f, s.st().heaterDuty);
  TEST_ASSERT_INT_WITHIN(ticks / 10, ticks / 2, on);
}

void test_brewing_after_switch_cycle() {
  Sim s;
  s.pinMv = pinMvForBar(0);
  s.begin();
  s.step();
  s.setCoffee(true);
  ASSERT_STATE(State::Brewing, s);
  TEST_ASSERT_TRUE(s.out.valve);
  TEST_ASSERT_TRUE(s.out.pump > 0);  // pressure 0 < 6 bar target
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_BREW_PRESSURE_BAR, s.st().pressureTargetBar);
  s.run(5000);
  TEST_ASSERT_UINT32_WITHIN(CONTROL_PERIOD_MS, 5000 + CONTROL_PERIOD_MS, s.st().shotMs);
  // Shot time stays visible after the shot.
  s.setCoffee(false);
  ASSERT_STATE(State::Idle, s);
  const uint32_t shot = s.st().shotMs;
  s.run(1000);
  TEST_ASSERT_EQUAL_UINT32(shot, s.st().shotMs);
  TEST_ASSERT_FALSE(s.out.valve);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
}

void test_pump_backs_off_when_over_pressure() {
  Sim s;
  s.begin();
  s.step();
  s.setCoffee(true);
  s.pinMv = pinMvForBar(11);  // well above the 6 bar target
  s.run(3000);
  ASSERT_STATE(State::Brewing, s);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
  TEST_ASSERT_TRUE(s.out.valve);
}

void test_coffee_on_at_boot_needs_cycle() {
  Sim s;
  s.coffee = true;
  s.begin();
  s.run(1000);
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_TRUE(s.st().waitSwitchCycle);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
  s.setCoffee(false);
  TEST_ASSERT_FALSE(s.st().waitSwitchCycle);
  s.setCoffee(true);
  ASSERT_STATE(State::Brewing, s);
}

void test_switch_glitch_is_debounced() {
  Sim s;
  s.begin();
  s.step();
  for (int i = 0; i < SWITCH_DEBOUNCE_TICKS - 1; ++i) {
    s.coffee = true;
    s.step();
  }
  s.coffee = false;
  s.run(500);
  ASSERT_STATE(State::Idle, s);
}

void test_steam_state() {
  Sim s;
  s.tempC = DEFAULT_STEAM_TEMP_C;
  s.begin();
  s.step();
  s.setSteam(true);
  ASSERT_STATE(State::Steam, s);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_STEAM_TEMP_C, s.st().setpointC);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
  TEST_ASSERT_FALSE(s.out.valve);
  s.run(TC_READ_PERIOD_MS);
  TEST_ASSERT_TRUE(s.out.steamLight);
  TEST_ASSERT_FALSE(s.out.coffeeLight);
}

void test_hot_water_state() {
  Sim s;
  s.begin();
  s.step();
  s.setSteam(true);
  s.setCoffee(true);
  ASSERT_STATE(State::HotWater, s);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_STEAM_TEMP_C, s.st().setpointC);
  TEST_ASSERT_EQUAL_UINT8(100, s.out.pump);
  TEST_ASSERT_FALSE(s.out.valve);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.st().pressureTargetBar);
}

void test_brewing_to_hot_water_keeps_pump_timer() {
  // Switching steam on mid-shot must not restart the pump run-time limit.
  Sim s;
  s.begin();
  s.step();
  s.setCoffee(true);
  s.run(PUMP_MAX_RUN_MS - 10000);
  s.setSteam(true);
  ASSERT_STATE(State::HotWater, s);
  s.run(10000);
  TEST_ASSERT_TRUE(s.st().pumpTimedOut);
  ASSERT_STATE(State::Steam, s);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
}

void test_pump_run_time_limit() {
  Sim s;
  s.begin();
  s.step();
  s.setCoffee(true);
  s.run(PUMP_MAX_RUN_MS - 1000);
  ASSERT_STATE(State::Brewing, s);
  s.run(2000);
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_TRUE(s.st().pumpTimedOut);
  TEST_ASSERT_TRUE(s.st().waitSwitchCycle);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
  TEST_ASSERT_FALSE(s.out.valve);
  // Cycling the switch re-arms.
  s.setCoffee(false);
  TEST_ASSERT_FALSE(s.st().pumpTimedOut);
  s.setCoffee(true);
  ASSERT_STATE(State::Brewing, s);
}

// ---- inhibit ---------------------------------------------------------------------
void test_inhibit_forces_loads_off_and_disarms() {
  Sim s;
  s.tempC = 25;
  s.begin();
  s.step();
  s.setCoffee(true);
  ASSERT_STATE(State::Brewing, s);
  s.inhibited = true;
  s.step();
  ASSERT_STATE(State::Inhibited, s);
  assertAllLoadsOff(s);
  TEST_ASSERT_FALSE(s.out.coffeeLight);
  s.run(1000);
  assertAllLoadsOff(s);
  // Update aborted: control resumes, but the Coffee switch (still on) must be cycled.
  s.inhibited = false;
  s.step();
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_TRUE(s.st().waitSwitchCycle);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
  s.run(TC_READ_PERIOD_MS);  // heater duty is recomputed on the next temperature sample
  TEST_ASSERT_TRUE(s.out.heater);  // heating resumes
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
}

// ---- safety faults ---------------------------------------------------------------
void test_over_temperature_fault_latches() {
  Sim s;
  s.tempC = 120;
  s.begin();
  s.run(1000);
  s.tempC = SAFETY_MAX_TEMP_C + 1;
  s.run(TC_READ_PERIOD_MS);
  assertFaultContains(s, "over-temperature");
  assertAllLoadsOff(s);
  // Latched: cooling down or switching doesn't clear it.
  s.tempC = 100;
  s.run(5000);
  s.setCoffee(true);
  ASSERT_STATE(State::Fault, s);
  assertAllLoadsOff(s);
}

void test_over_temperature_with_heater_off_blames_ssr() {
  Sim s;
  s.tempC = SAFETY_MAX_TEMP_C + 5;
  s.begin();
  s.step();
  assertFaultContains(s, "SSR may be shorted");
}

void test_fault_lights_blink() {
  Sim s;
  s.tempC = SAFETY_MAX_TEMP_C + 5;
  s.begin();
  int on = 0, off = 0;
  for (int i = 0; i < 40; ++i) {
    s.step();
    TEST_ASSERT_EQUAL(s.out.coffeeLight, s.out.steamLight);
    s.out.coffeeLight ? ++on : ++off;
  }
  TEST_ASSERT_TRUE(on > 5 && off > 5);
}

void test_fault_keeps_first_cause() {
  Sim s;
  s.tempC = SAFETY_MAX_TEMP_C + 5;
  s.begin();
  s.step();
  s.tcError = "thermocouple open circuit";
  s.run(5000);
  assertFaultContains(s, "over-temperature");
}

void test_thermocouple_failures() {
  Sim s;
  s.begin();
  s.step();
  s.tcError = "thermocouple open circuit";
  // SAFETY_TC_FAIL_COUNT - 1 bad reads: still running, last good temperature kept.
  const int start = s.tcReads;
  while (s.tcReads - start < SAFETY_TC_FAIL_COUNT - 1) s.step();
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_TRUE(s.st().tempValid);
  // A good read resets the count.
  s.tcError = nullptr;
  s.run(TC_READ_PERIOD_MS);
  s.tcError = "thermocouple open circuit";
  const int again = s.tcReads;
  while (s.tcReads - again < SAFETY_TC_FAIL_COUNT - 1) s.step();
  ASSERT_STATE(State::Idle, s);
  // One more consecutive failure -> fault.
  const int last = s.tcReads;
  while (s.tcReads == last) s.step();
  assertFaultContains(s, "Temperature sensor: thermocouple open circuit");
  TEST_ASSERT_FALSE(s.st().tempValid);
  assertAllLoadsOff(s);
}

void test_no_zero_cross_while_pumping() {
  Sim s;
  s.begin();
  s.step();
  s.zeroCross = false;  // dimmer unpowered
  s.run(2000);
  ASSERT_STATE(State::Idle, s);  // not checked while the pump is off
  s.setCoffee(true);
  s.run(ZC_TIMEOUT_MS + 2 * CONTROL_PERIOD_MS);
  assertFaultContains(s, "zero-cross");
  assertAllLoadsOff(s);
}

void test_zero_cross_ok_while_pumping() {
  Sim s;
  s.begin();
  s.step();
  s.setCoffee(true);
  s.run(10000);
  ASSERT_STATE(State::Brewing, s);
}

void test_pressure_sensor_fault_only_while_brewing() {
  Sim s;
  s.begin();
  s.step();
  s.pinMv = 0;  // sensor disconnected
  s.run(2000);
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_FALSE(s.st().pressureValid);
  s.setCoffee(true);
  s.run(PRESSURE_FAULT_MS + CONTROL_PERIOD_MS);
  assertFaultContains(s, "Pressure sensor out of range");
}

void test_pressure_glitch_shorter_than_limit_is_ignored() {
  Sim s;
  s.begin();
  s.step();
  s.setCoffee(true);
  s.pinMv = 0;
  s.run(PRESSURE_FAULT_MS - 2 * CONTROL_PERIOD_MS);
  s.pinMv = pinMvForBar(6);
  s.run(1000);
  ASSERT_STATE(State::Brewing, s);
}

void test_heater_check_fault_when_temperature_stuck() {
  Sim s;
  s.tempC = 30;  // full heater power, but the temperature never rises
  s.begin();
  s.run(HEAT_CHECK_WINDOW_MS - 1000);
  ASSERT_STATE(State::Idle, s);
  s.run(2000);
  assertFaultContains(s, "Boiler not heating");
}

void test_heater_check_passes_when_heating() {
  Sim s;
  s.tempC = 30;
  s.begin();
  for (int i = 0; i < 3; ++i) {
    s.run(HEAT_CHECK_WINDOW_MS / 2);
    s.tempC += HEAT_CHECK_MIN_RISE_C;  // +2 C per half window
  }
  ASSERT_STATE(State::Idle, s);
}

void test_heater_check_paused_while_pumping() {
  // Cold water inflow during a long hot-water draw must not trip the check.
  Sim s;
  s.tempC = 30;
  s.begin();
  s.step();
  s.setSteam(true);
  s.setCoffee(true);
  s.run(PUMP_MAX_RUN_MS - 5000);
  TEST_ASSERT_EQUAL_STRING("", s.st().fault);
}

// ---- measurements ----------------------------------------------------------------
void test_pressure_smoothing() {
  Sim s;
  s.pinMv = pinMvForBar(0);
  s.begin();
  s.step();
  s.pinMv = pinMvForBar(9);
  s.step();
  const float first = s.st().pressureBar;
  TEST_ASSERT_TRUE(first > 0.5f && first < 9.0f);  // filtered, not a jump
  s.run(1500);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 9.0f, s.st().pressureBar);
  TEST_ASSERT_TRUE(s.st().pressureValid);
}

void test_pressure_pin_mv_shows_live_value_when_invalid() {
  Sim s;
  s.pinMv = pinMvForBar(6);
  s.begin();
  s.run(1000);
  s.pinMv = 50;
  s.step();
  TEST_ASSERT_FALSE(s.st().pressureValid);
  TEST_ASSERT_EQUAL_UINT16(50, s.st().pressurePinMv);
}

void test_pressure_supply_correction_applied() {
  // Ratiometric sensor at 4.8 V supply reading 6 bar; with the supply measured the
  // control loop must see 6 bar, not the ~5.76 bar the raw voltage suggests.
  Sim s;
  const float supply = 4.8f;
  s.pinMv = pinMvForBar(6) * supply / PRESSURE_SUPPLY_NOMINAL_V;
  s.supplyPinMv = supply * 1000.0f * PRESSURE_DIVIDER_RATIO;
  s.begin();
  s.run(2000);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 6.0f, s.st().pressureBar);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, supply, s.st().pressureSupplyV);
}

void test_pressure_converter_failure_fault_message() {
  Sim s;
  s.begin();
  s.step();
  s.setCoffee(true);
  s.pressError = "ADS1115 not responding";
  s.run(PRESSURE_FAULT_MS + CONTROL_PERIOD_MS);
  assertFaultContains(s, "Pressure sensor: ADS1115 not responding");
  assertAllLoadsOff(s);
}

void test_mains_frequency() {
  Sim s;
  s.begin();
  s.run(2000);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 60.0f, s.st().mainsHz);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_idle_and_heats_to_coffee_setpoint);
  RUN_TEST(test_ready_light_within_band);
  RUN_TEST(test_heater_off_above_setpoint);
  RUN_TEST(test_heater_time_proportioning);
  RUN_TEST(test_brewing_after_switch_cycle);
  RUN_TEST(test_pump_backs_off_when_over_pressure);
  RUN_TEST(test_coffee_on_at_boot_needs_cycle);
  RUN_TEST(test_switch_glitch_is_debounced);
  RUN_TEST(test_steam_state);
  RUN_TEST(test_hot_water_state);
  RUN_TEST(test_brewing_to_hot_water_keeps_pump_timer);
  RUN_TEST(test_pump_run_time_limit);
  RUN_TEST(test_inhibit_forces_loads_off_and_disarms);
  RUN_TEST(test_over_temperature_fault_latches);
  RUN_TEST(test_over_temperature_with_heater_off_blames_ssr);
  RUN_TEST(test_fault_lights_blink);
  RUN_TEST(test_fault_keeps_first_cause);
  RUN_TEST(test_thermocouple_failures);
  RUN_TEST(test_no_zero_cross_while_pumping);
  RUN_TEST(test_zero_cross_ok_while_pumping);
  RUN_TEST(test_pressure_sensor_fault_only_while_brewing);
  RUN_TEST(test_pressure_glitch_shorter_than_limit_is_ignored);
  RUN_TEST(test_heater_check_fault_when_temperature_stuck);
  RUN_TEST(test_heater_check_passes_when_heating);
  RUN_TEST(test_heater_check_paused_while_pumping);
  RUN_TEST(test_pressure_smoothing);
  RUN_TEST(test_pressure_pin_mv_shows_live_value_when_invalid);
  RUN_TEST(test_pressure_supply_correction_applied);
  RUN_TEST(test_pressure_converter_failure_fault_message);
  RUN_TEST(test_mains_frequency);
  return UNITY_END();
}
