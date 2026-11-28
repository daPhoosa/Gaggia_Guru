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

// Thermocouple reading that puts the boiler at the coffee target (brew temp offset applied).
constexpr float kCoffeeBoilerC = DEFAULT_COFFEE_TEMP_C + DEFAULT_BREW_TEMP_OFFSET_C;

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
  float tempC = kCoffeeBoilerC;  // thermocouple
  const char *tcError = nullptr;  // non-null: thermocouple reads fail with this
  float pinMv = pinMvForBar(0);
  float supplyPinMv = 0;          // > 0: ADS1115-style reading with supply measurement
  const char *pressError = nullptr;  // non-null: converter failure
  bool zeroCross = true;          // dimmer zero-cross pulses arriving
  bool inhibited = false;
  bool testWanted = false;        // test page request (dropped like machine.cpp does)
  uint32_t testIdleMs = 0;
  Machine::TestCommand testCmd{};

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
    in.testWanted = testWanted;
    in.testIdleMs = testIdleMs;
    in.testCmd = testCmd;
    out = m.tick(in, cfg);
    // machine.cpp drops the request once TEST ends or is refused.
    if (testWanted && m.status().state != State::Test) testWanted = false;
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
  s.tempC = kCoffeeBoilerC - DEFAULT_READY_BAND_C + 0.25f;
  s.begin();
  s.step();
  TEST_ASSERT_TRUE(s.out.coffeeLight);
  TEST_ASSERT_FALSE(s.out.steamLight);
  s.tempC = kCoffeeBoilerC - DEFAULT_READY_BAND_C - 0.5f;
  s.run(2000);  // let the EMA filter settle (~0.56 s time constant)
  TEST_ASSERT_FALSE(s.out.coffeeLight);
}

void test_ready_light_stays_on_above_target() {
  Sim s;
  s.tempC = kCoffeeBoilerC;
  s.begin();
  s.run(2000);
  TEST_ASSERT_TRUE(s.out.coffeeLight);
  s.tempC = kCoffeeBoilerC + DEFAULT_READY_BAND_C + 5;  // overshoot: stays on
  s.run(2000);
  TEST_ASSERT_TRUE(s.out.coffeeLight);
  s.tempC = kCoffeeBoilerC - DEFAULT_READY_BAND_C - 1;  // below the band: off
  s.run(2000);
  TEST_ASSERT_FALSE(s.out.coffeeLight);
}

void test_coffee_light_waits_for_boiler_to_cool_after_steam() {
  Sim s;
  s.tempC = DEFAULT_STEAM_TEMP_C;
  s.begin();
  s.step();
  s.setSteam(true);
  s.run(2000);
  TEST_ASSERT_TRUE(s.out.steamLight);
  s.tempC = DEFAULT_STEAM_TEMP_C + 5;  // above the steam target, below SAFETY_MAX_TEMP_C
  s.run(2000);
  TEST_ASSERT_TRUE(s.out.steamLight);
  s.setSteam(false);  // back to coffee, boiler still hot
  s.run(2000);
  TEST_ASSERT_FALSE(s.out.coffeeLight);
  TEST_ASSERT_FALSE(s.out.steamLight);
  s.tempC = kCoffeeBoilerC + DEFAULT_READY_BAND_C - 0.5f;  // cooled into the band
  s.run(5000);  // let the filtered reading settle
  TEST_ASSERT_TRUE(s.out.coffeeLight);
}

void test_ready_band_follows_setting() {
  Sim s;
  s.cfg.readyBandC = 1.0f;
  s.tempC = kCoffeeBoilerC - 2.0f;
  s.begin();
  s.run(2000);
  TEST_ASSERT_FALSE(s.out.coffeeLight);  // outside +/-1
  s.cfg.readyBandC = 5.0f;
  s.step();
  TEST_ASSERT_TRUE(s.out.coffeeLight);  // inside +/-5
}

void test_temp_filter_damps_spike_and_settles() {
  Sim s;
  s.tempC = 100;
  s.begin();
  s.step();
  TEST_ASSERT_EQUAL_FLOAT(100.0f, s.st().tempC);  // first sample seeds the filter
  s.run(TEMP_MEDIAN_WINDOW * TC_READ_PERIOD_MS);  // fill the median window
  s.tempC = 110;                                  // two-sample spike: removed by the median
  s.run(2 * TC_READ_PERIOD_MS);
  TEST_ASSERT_EQUAL_FLOAT(110.0f, s.st().tempRawC);
  if (TEMP_MEDIAN_WINDOW >= 5) TEST_ASSERT_EQUAL_FLOAT(100.0f, s.st().tempC);  // else the EMA lets part of it in
  else TEST_ASSERT_TRUE(s.st().tempC > 100.0f && s.st().tempC < 110.0f);
  s.tempC = 100;
  s.run(3000);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 100.0f, s.st().tempC);
}

void test_temp_filter_follows_step() {
  // A real step gets through the median after TEMP_MEDIAN_WINDOW / 2 + 1
  // samples, then the EMA takes it in at TEMP_EMA_ALPHA per sample.
  Sim s;
  s.tempC = 100;
  s.begin();
  s.run(2000);
  s.tempC = 110;
  s.run((TEMP_MEDIAN_WINDOW / 2) * TC_READ_PERIOD_MS);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, s.st().tempC);  // median still holds the old value
  s.run(TC_READ_PERIOD_MS);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f + TEMP_EMA_ALPHA * 10, s.st().tempC);
  s.run(5000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 110.0f, s.st().tempC);
}

void test_over_temp_trips_on_raw_reading() {
  // The trip must not wait for the filter: one raw sample over the limit faults.
  Sim s;
  s.tempC = SAFETY_MAX_TEMP_C - 10;
  s.begin();
  s.run(1000);
  s.tempC = SAFETY_MAX_TEMP_C + 1;
  s.run(TC_READ_PERIOD_MS);
  TEST_ASSERT_TRUE(s.st().tempC < SAFETY_MAX_TEMP_C);  // filtered value still below
  TEST_ASSERT_EQUAL(State::Fault, s.st().state);
}

void test_heater_off_above_setpoint() {
  Sim s;
  s.tempC = kCoffeeBoilerC + 5;
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
  s.tempC = kCoffeeBoilerC - 5;
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
    // The check windows line up with these steps, so a window may see only
    // one step (the next one is still in the median/EMA delay). Each step
    // must exceed the minimum rise on its own.
    s.tempC += HEAT_CHECK_MIN_RISE_C + 0.5f;
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

void test_pressure_median_removes_short_spike() {
  Sim s;
  s.pinMv = pinMvForBar(6);
  s.begin();
  s.run(1500);  // fill the median window and settle the EMA
  const float settled = s.st().pressureBar;
  s.pinMv = pinMvForBar(11);  // two-tick spike
  s.step();
  s.step();
  s.pinMv = pinMvForBar(6);
  if (PRESS_MEDIAN_WINDOW >= 5) TEST_ASSERT_FLOAT_WITHIN(0.01f, settled, s.st().pressureBar);
  else TEST_ASSERT_TRUE(s.st().pressureBar > settled);  // no median: the EMA lets part of it in
  s.run(1500);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 6.0f, s.st().pressureBar);
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

// ---- brew temp offset ------------------------------------------------------------
void test_brew_offset_in_coffee_mode() {
  Sim s;
  s.tempC = 100;  // thermocouple
  s.begin();
  s.step();
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_COFFEE_TEMP_C, s.st().setpointC);  // shown target
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_BREW_TEMP_OFFSET_C, s.st().tempOffsetC);
  TEST_ASSERT_EQUAL_FLOAT(kCoffeeBoilerC, s.st().boilerSetpointC);    // PID target
  TEST_ASSERT_EQUAL_FLOAT(100.0f - DEFAULT_BREW_TEMP_OFFSET_C, Machine::displayTempC(s.st()));

  // Shown temp at the target but thermocouple below target + offset: still heating.
  s.cfg.tempKp = 10;
  s.cfg.tempKi = 0;
  s.cfg.tempKd = 0;
  s.cfg.coffeeTempC = 95;
  s.cfg.brewTempOffsetC = 10;
  s.tempC = 100;  // shows 90, PID target 105
  s.run(HEATER_WINDOW_MS);
  TEST_ASSERT_EQUAL_FLOAT(90.0f, Machine::displayTempC(s.st()));
  TEST_ASSERT_EQUAL_FLOAT(105.0f, s.st().boilerSetpointC);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, s.st().heaterDuty);  // 5 C below 105 at 10 %/C
  TEST_ASSERT_FALSE(s.out.coffeeLight);

  // Offset carries into BREWING.
  s.setCoffee(true);
  ASSERT_STATE(State::Brewing, s);
  TEST_ASSERT_EQUAL_FLOAT(105.0f, s.st().boilerSetpointC);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, s.st().tempOffsetC);
}

void test_brew_bias_raises_pid_target_only_while_brewing() {
  Sim s;
  s.cfg.tempKp = 10;
  s.cfg.tempKi = 0;
  s.cfg.tempKd = 0;
  s.tempC = kCoffeeBoilerC - 5;
  s.begin();
  s.step();
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_EQUAL_FLOAT(kCoffeeBoilerC, s.st().pidTargetC);  // no bias when idle
  s.setCoffee(true);
  s.run(HEATER_WINDOW_MS);
  ASSERT_STATE(State::Brewing, s);
  TEST_ASSERT_EQUAL_FLOAT(kCoffeeBoilerC + BREW_TEMP_BIAS_C, s.st().pidTargetC);
  TEST_ASSERT_EQUAL_FLOAT(kCoffeeBoilerC, s.st().boilerSetpointC);    // ready light target unchanged
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_COFFEE_TEMP_C, s.st().setpointC);   // shown target unchanged
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f * (5 + BREW_TEMP_BIAS_C), s.st().heaterDuty);
  s.setCoffee(false);
  s.run(HEATER_WINDOW_MS);
  TEST_ASSERT_EQUAL_FLOAT(kCoffeeBoilerC, s.st().pidTargetC);
}

void test_brew_offset_ready_light_uses_offset_target() {
  Sim s;
  s.tempC = DEFAULT_COFFEE_TEMP_C;  // shown temp would be 7 C low
  s.begin();
  s.step();
  TEST_ASSERT_FALSE(s.out.coffeeLight);
  s.tempC = kCoffeeBoilerC;
  s.run(2000);
  TEST_ASSERT_TRUE(s.out.coffeeLight);
}

void test_steam_ignores_brew_offset() {
  Sim s;
  s.tempC = DEFAULT_STEAM_TEMP_C;
  s.begin();
  s.step();
  s.setSteam(true);
  s.run(TC_READ_PERIOD_MS);
  ASSERT_STATE(State::Steam, s);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.st().tempOffsetC);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_STEAM_TEMP_C, s.st().boilerSetpointC);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_STEAM_TEMP_C, Machine::displayTempC(s.st()));
  TEST_ASSERT_TRUE(s.out.steamLight);
  s.setCoffee(true);  // HOT_WATER: also no offset
  ASSERT_STATE(State::HotWater, s);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.st().tempOffsetC);
  TEST_ASSERT_EQUAL_FLOAT(DEFAULT_STEAM_TEMP_C, s.st().boilerSetpointC);
}

void test_over_temp_trip_ignores_brew_offset() {
  // A large offset makes the shown temp read low; the trip uses the thermocouple.
  Sim s;
  s.cfg.brewTempOffsetC = LIMIT_BREW_TEMP_OFFSET_MAX;
  s.tempC = SAFETY_MAX_TEMP_C + 1;
  s.begin();
  s.step();
  TEST_ASSERT_TRUE(Machine::displayTempC(s.st()) < SAFETY_MAX_TEMP_C);
  assertFaultContains(s, "over-temperature");
  assertAllLoadsOff(s);
}

// ---- output test mode ------------------------------------------------------------
static void startTest(Sim &s) {
  s.testWanted = true;
  s.step();
}

void test_test_mode_applies_manual_outputs() {
  Sim s;
  s.tempC = 100;
  s.begin();
  s.run(500);
  s.testCmd = {true, true, true, true, false, 60};
  startTest(s);
  ASSERT_STATE(State::Test, s);
  s.run(200);
  TEST_ASSERT_TRUE(s.out.heater);
  TEST_ASSERT_EQUAL_FLOAT(100.0f, s.st().heaterDuty);
  TEST_ASSERT_TRUE(s.out.valve);
  TEST_ASSERT_EQUAL_UINT8(60, s.out.pump);
  TEST_ASSERT_TRUE(s.out.coffeeLight);
  TEST_ASSERT_FALSE(s.out.steamLight);
  TEST_ASSERT_EQUAL_STRING("", s.st().testEnd);

  s.testCmd = {false, false, false, false, true, 60};
  s.run(200);
  TEST_ASSERT_FALSE(s.out.heater);
  TEST_ASSERT_FALSE(s.out.valve);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
  TEST_ASSERT_TRUE(s.out.steamLight);
}

void test_test_mode_refused_with_switch_on() {
  Sim s;
  s.begin();
  s.setSteam(true);
  startTest(s);
  ASSERT_STATE(State::Steam, s);
  TEST_ASSERT_NOT_NULL(strstr(s.st().testEnd, "switches off first"));
  TEST_ASSERT_FALSE(s.testWanted);
}

void test_test_mode_stop_returns_to_idle() {
  Sim s;
  s.begin();
  s.testCmd = {false, true, true, false, false, 100};
  startTest(s);
  s.run(200);
  s.testWanted = false;
  s.step();
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_EQUAL_STRING("Stopped", s.st().testEnd);
  TEST_ASSERT_FALSE(s.out.valve);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
}

void test_test_mode_coffee_switch_ends_without_brewing() {
  Sim s;
  s.begin();
  s.testCmd = {false, false, true, false, false, 100};
  startTest(s);
  s.setCoffee(true);
  ASSERT_STATE(State::Idle, s);  // must cycle Coffee before a shot
  TEST_ASSERT_TRUE(s.st().waitSwitchCycle);
  TEST_ASSERT_NOT_NULL(strstr(s.st().testEnd, "switch turned on"));
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
  TEST_ASSERT_FALSE(s.out.valve);
  s.setCoffee(false);
  s.setCoffee(true);
  ASSERT_STATE(State::Brewing, s);
}

void test_test_mode_steam_switch_ends() {
  Sim s;
  s.begin();
  startTest(s);
  s.setSteam(true);
  ASSERT_STATE(State::Steam, s);
  TEST_ASSERT_NOT_NULL(strstr(s.st().testEnd, "switch turned on"));
}

void test_test_mode_ends_when_page_goes_quiet() {
  Sim s;
  s.begin();
  s.testCmd = {true, false, false, false, false, 100};
  startTest(s);
  s.testIdleMs = TEST_TIMEOUT_MS;
  s.step();
  ASSERT_STATE(State::Test, s);
  s.testIdleMs = TEST_TIMEOUT_MS + 1;
  s.step();
  ASSERT_STATE(State::Idle, s);
  TEST_ASSERT_EQUAL_STRING("Test page connection lost", s.st().testEnd);
}

void test_test_mode_heater_cap_with_hysteresis() {
  Sim s;
  s.tempC = TEST_HEATER_MAX_C - 5;
  s.begin();
  s.testCmd = {true, false, false, false, false, 100};
  startTest(s);
  s.run(200);
  TEST_ASSERT_TRUE(s.out.heater);
  TEST_ASSERT_FALSE(s.st().testHeaterCapped);
  s.tempC = TEST_HEATER_MAX_C + 1;  // still below the 155 C trip
  s.run(3000);
  TEST_ASSERT_FALSE(s.out.heater);
  TEST_ASSERT_TRUE(s.st().testHeaterCapped);
  ASSERT_STATE(State::Test, s);
  s.tempC = TEST_HEATER_MAX_C - 1;  // inside the hysteresis band: stays off
  s.run(3000);
  TEST_ASSERT_FALSE(s.out.heater);
  s.tempC = TEST_HEATER_MAX_C - 3;
  s.run(3000);
  TEST_ASSERT_TRUE(s.out.heater);
  TEST_ASSERT_FALSE(s.st().testHeaterCapped);
}

void test_test_mode_pump_run_time_limit() {
  Sim s;
  s.begin();
  s.testCmd = {false, false, true, false, false, 100};
  startTest(s);
  s.run(PUMP_MAX_RUN_MS - 1000);
  TEST_ASSERT_EQUAL_UINT8(100, s.out.pump);
  s.run(2000);
  TEST_ASSERT_EQUAL_UINT8(0, s.out.pump);
  TEST_ASSERT_TRUE(s.st().testPumpTimedOut);
  TEST_ASSERT_FALSE(s.st().pumpTimedOut);  // the Coffee-switch banner is not involved
  ASSERT_STATE(State::Test, s);
  s.testCmd.pump = false;  // toggle off and on again
  s.step();
  TEST_ASSERT_FALSE(s.st().testPumpTimedOut);
  s.testCmd.pump = true;
  s.step();
  TEST_ASSERT_EQUAL_UINT8(100, s.out.pump);
}

void test_test_mode_safety_faults_still_apply() {
  Sim s;
  s.tempC = 100;
  s.begin();
  s.testCmd = {true, true, true, true, true, 100};
  startTest(s);
  s.run(200);
  s.tempC = SAFETY_MAX_TEMP_C + 1;
  s.run(TC_READ_PERIOD_MS);
  assertFaultContains(s, "over-temperature");
  assertAllLoadsOff(s);
  TEST_ASSERT_EQUAL_STRING("Fault", s.st().testEnd);

  Sim z;
  z.begin();
  z.testCmd = {false, false, true, false, false, 100};
  startTest(z);
  z.zeroCross = false;
  z.run(ZC_TIMEOUT_MS + 200);
  assertFaultContains(z, "zero-cross");
  assertAllLoadsOff(z);
}

void test_test_mode_ends_on_inhibit() {
  Sim s;
  s.begin();
  s.testCmd = {true, true, true, false, false, 100};
  startTest(s);
  s.inhibited = true;
  s.step();
  ASSERT_STATE(State::Inhibited, s);
  assertAllLoadsOff(s);
  TEST_ASSERT_EQUAL_STRING("Firmware update / reboot", s.st().testEnd);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_idle_and_heats_to_coffee_setpoint);
  RUN_TEST(test_ready_light_within_band);
  RUN_TEST(test_ready_band_follows_setting);
  RUN_TEST(test_ready_light_stays_on_above_target);
  RUN_TEST(test_coffee_light_waits_for_boiler_to_cool_after_steam);
  RUN_TEST(test_temp_filter_damps_spike_and_settles);
  RUN_TEST(test_temp_filter_follows_step);
  RUN_TEST(test_over_temp_trips_on_raw_reading);
  RUN_TEST(test_heater_off_above_setpoint);
  RUN_TEST(test_heater_time_proportioning);
  RUN_TEST(test_brew_offset_in_coffee_mode);
  RUN_TEST(test_brew_offset_ready_light_uses_offset_target);
  RUN_TEST(test_brew_bias_raises_pid_target_only_while_brewing);
  RUN_TEST(test_steam_ignores_brew_offset);
  RUN_TEST(test_over_temp_trip_ignores_brew_offset);
  RUN_TEST(test_brewing_after_switch_cycle);
  RUN_TEST(test_test_mode_applies_manual_outputs);
  RUN_TEST(test_test_mode_refused_with_switch_on);
  RUN_TEST(test_test_mode_stop_returns_to_idle);
  RUN_TEST(test_test_mode_coffee_switch_ends_without_brewing);
  RUN_TEST(test_test_mode_steam_switch_ends);
  RUN_TEST(test_test_mode_ends_when_page_goes_quiet);
  RUN_TEST(test_test_mode_heater_cap_with_hysteresis);
  RUN_TEST(test_test_mode_pump_run_time_limit);
  RUN_TEST(test_test_mode_safety_faults_still_apply);
  RUN_TEST(test_test_mode_ends_on_inhibit);
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
  RUN_TEST(test_pressure_median_removes_short_spike);
  RUN_TEST(test_pressure_pin_mv_shows_live_value_when_invalid);
  RUN_TEST(test_pressure_supply_correction_applied);
  RUN_TEST(test_pressure_converter_failure_fault_message);
  RUN_TEST(test_mains_frequency);
  return UNITY_END();
}
