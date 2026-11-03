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

enum class State : uint8_t { Idle, Brewing, Steam, HotWater, Fault, Inhibited };

const char *stateName(State s);

struct Status {
  State state;
  bool tempValid;
  float tempC;
  float setpointC;
  bool pressureValid;
  float pressureBar;
  float pressureTargetBar;  // 0 unless brewing
  uint16_t pressurePinMv;   // pin voltage: smoothed when valid, live sample when out of range
  float pressureSupplyV;    // measured sensor supply (ADS1115 option), 0 if not measured
  float heaterDuty;         // 0-100 %
  uint8_t pumpPower;        // 0-100 %
  bool heaterOn, valveOn, coffeeLight, steamLight;
  bool coffeeSwitch, steamSwitch;  // debounced
  bool waitSwitchCycle;     // Coffee switch is on but ignored until cycled off/on
  bool pumpTimedOut;
  uint32_t shotMs;          // current shot time, or last shot when idle
  float mainsHz;
  char fault[112];          // empty unless FAULT
  char inhibitReason[32];   // filled in by machine.cpp
};

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
};

}  // namespace Machine
