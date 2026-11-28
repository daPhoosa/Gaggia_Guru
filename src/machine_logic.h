#pragma once
// Gaggia Guru control logic: state machine, heater/pump PID and software
// safety checks, with no hardware access (no Arduino headers).
//
// machine.cpp reads the pins and sensors into Inputs once per control tick,
// calls Logic::tick(), and writes the returned Outputs to the loads. Because
// this file touches no hardware, every state change and fault path is
// unit-tested on the PC (pio test -e native).
#include <stddef.h>
#include <stdint.h>

#include "machine_config.h"
#include "machine_settings.h"
#include "pid.h"
#include "sensor_math.h"

namespace Machine {

enum class State : uint8_t { Idle, Brewing, Steam, HotWater, Fault, Inhibited, Test };

// Output test (password-protected web test page): the outputs it asks for.
// Logic::tick applies them only in State::Test, with the safety checks still
// running, the heater held off above TEST_HEATER_MAX_C and the pump limited to
// PUMP_MAX_RUN_MS of continuous running.
struct TestCommand {
  bool heater, valve, pump, coffeeLight, steamLight;
  uint8_t pumpPct;  // pump power while on, 0-100 %
};

const char *stateName(State s);

struct Status {
  State state;
  bool tempValid;
  float tempC;     // thermocouple, median (TEMP_MEDIAN_WINDOW) then EMA (TEMP_EMA_ALPHA): PID, heater check
  float tempRawC;  // thermocouple, last good unfiltered reading: over-temperature trip
  float tempOffsetC;      // brew temp offset in use: cfg.brewTempOffsetC in coffee mode, 0 in steam / hot water
  float setpointC;        // user target as displayed (coffee = water at the portafilter)
  float boilerSetpointC;  // boiler target on the thermocouple = setpointC + tempOffsetC (ready light)
  float pidTargetC;       // heater PID target: boilerSetpointC, + BREW_TEMP_BIAS_C while BREWING
  bool pressureValid;
  float pressureBar;
  float pressureTargetBar;  // 0 unless brewing
  uint16_t pressurePinMv;   // pin voltage: smoothed when valid, live sample when out of range
  float pressureSupplyV;    // measured sensor supply (ADS1115 AIN1), 0 if not measured
  float heaterDuty;         // 0-100 %
  uint8_t pumpPower;        // 0-100 %
  bool heaterOn, valveOn, coffeeLight, steamLight;
  bool coffeeSwitch, steamSwitch;  // debounced
  bool waitSwitchCycle;     // Coffee switch is on but ignored until cycled off/on
  bool pumpTimedOut;
  bool testPumpTimedOut;    // TEST: pump stopped by the run-time limit; toggle it off to reset
  bool testHeaterCapped;    // TEST: manual heater held off (above TEST_HEATER_MAX_C, 2 C hysteresis, or no valid temp)
  char testEnd[48];         // why the last test ended or was refused (empty while TEST or never used)
  uint32_t shotMs;          // current shot time, or last shot when idle
  float mainsHz;
  char fault[112];          // empty unless FAULT
  char inhibitReason[32];   // filled in by machine.cpp
};

// Temperature shown to the user: thermocouple minus the brew temp offset in
// coffee mode (estimated water at the portafilter), plain thermocouple otherwise.
inline float displayTempC(const Status &s) { return s.tempC - s.tempOffsetC; }

// Everything the logic needs from the hardware for one tick.
struct Inputs {
  uint32_t nowMs;
  bool coffeeSwRaw, steamSwRaw;  // raw pin levels (true = switch on); debounced here
  bool tcSampled;                // thermocouple was read this tick (see Logic::tcDue)
  Sensors::TcReading tc;         // valid only when tcSampled
  Sensors::PressureReading pressure;
  uint32_t msSinceZeroCross;     // from the pump dimmer's zero-cross input
  uint32_t zeroCrossCount;       // running count of zero-crossings
  bool inhibited;                // firmware update / reboot in progress
  bool testWanted;               // test page wants manual control (start / keep it going)
  uint32_t testIdleMs;           // ms since the test page last checked in
  TestCommand testCmd;
};

// What to drive this tick.
struct Outputs {
  bool heater;
  bool valve;
  uint8_t pump;  // 0-100 %
  bool coffeeLight;
  bool steamLight;
};

class Logic {
 public:
  // Start in IDLE from the real switch positions. The Coffee switch must be
  // seen OFF before it can start the pump.
  void begin(uint32_t nowMs, bool coffeeSwRaw, bool steamSwRaw, uint32_t zeroCrossCount);

  // True when the thermocouple should be read this tick (every TC_READ_PERIOD_MS).
  bool tcDue(uint32_t nowMs) const { return nowMs - lastTcMs_ >= TC_READ_PERIOD_MS; }

  Outputs tick(const Inputs &in, const MachineSettings &cfg);

  const Status &status() const { return st_; }

 private:
  void setFault(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void debounce(bool raw, bool &state, uint8_t &cnt);
  bool testAllowed(const Inputs &in);  // sets st_.testEnd when it says no

  Status st_{};
  Pid tempPid_, pressPid_;

  bool faulted_ = false;
  bool coffeeArmed_ = false;  // Coffee switch has been seen OFF
  bool pumpRunning_ = false;
  uint32_t pumpStartMs_ = 0;
  uint32_t shotStartMs_ = 0;

  bool coffeeSw_ = false, steamSw_ = false;
  uint8_t coffeeCnt_ = 0, steamCnt_ = 0;

  uint32_t lastTcMs_ = 0, lastTcOkMs_ = 0;
  uint8_t tcFails_ = 0;
  Sensors::MedianFilter<TEMP_MEDIAN_WINDOW> tempMedian_;
  Sensors::MedianFilter<PRESS_MEDIAN_WINDOW> pressMedian_;
  float pressPinMvFilt_ = 0;  // EMA-smoothed pin voltage (mV)
  float pressScale_ = 1.0f;   // last valid ratiometric supply correction
  bool pressPrimed_ = false;
  uint32_t pressBadSinceMs_ = 0;
  bool pressBad_ = false;

  uint32_t heaterWindowStart_ = 0;
  bool heatCheckActive_ = false;
  uint32_t heatCheckStartMs_ = 0;
  float heatCheckStartTemp_ = 0;

  uint32_t hzWindowStart_ = 0, hzZcStart_ = 0;

  bool testPumpStopped_ = false;  // run-time limit hit; cleared when the pump is toggled off
};

}  // namespace Machine
