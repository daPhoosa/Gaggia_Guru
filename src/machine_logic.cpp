#include "machine_logic.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Machine {

namespace {
bool isPumpState(State s) { return s == State::Brewing || s == State::HotWater; }
bool isSteamState(State s) { return s == State::Steam || s == State::HotWater; }
}  // namespace

const char *stateName(State s) {
  switch (s) {
    case State::Idle: return "IDLE";
    case State::Brewing: return "BREWING";
    case State::Steam: return "STEAM";
    case State::HotWater: return "HOT_WATER";
    case State::Fault: return "FAULT";
    case State::Inhibited: return "INHIBITED";
  }
  return "?";
}

void Logic::begin(uint32_t nowMs, bool coffeeSwRaw, bool steamSwRaw, uint32_t zeroCrossCount) {
  *this = Logic();
  coffeeSw_ = coffeeSwRaw;  // start from the real positions
  steamSw_ = steamSwRaw;
  coffeeArmed_ = false;     // must see Coffee OFF before brewing
  st_.state = State::Idle;
  st_.coffeeSwitch = coffeeSw_;
  st_.steamSwitch = steamSw_;
  tempPid_.setOutputLimits(0, 100);
  pressPid_.setOutputLimits(0, 100);
  heaterWindowStart_ = hzWindowStart_ = nowMs;
  hzZcStart_ = zeroCrossCount;
  lastTcMs_ = nowMs - TC_READ_PERIOD_MS;  // first tick reads the thermocouple
}

void Logic::setFault(const char *fmt, ...) {
  if (faulted_) return;  // keep the first cause
  faulted_ = true;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(st_.fault, sizeof(st_.fault), fmt, ap);
  va_end(ap);
}

void Logic::debounce(bool raw, bool &state, uint8_t &cnt) {
  if (raw == state) {
    cnt = 0;
  } else if (++cnt >= SWITCH_DEBOUNCE_TICKS) {
    state = raw;
    cnt = 0;
  }
}

Outputs Logic::tick(const Inputs &in, const MachineSettings &cfg) {
  const uint32_t now = in.nowMs;
  constexpr float dtTick = CONTROL_PERIOD_MS / 1000.0f;

  // --- inputs ---
  debounce(in.coffeeSwRaw, coffeeSw_, coffeeCnt_);
  debounce(in.steamSwRaw, steamSw_, steamCnt_);
  st_.coffeeSwitch = coffeeSw_;
  st_.steamSwitch = steamSw_;

  bool newTemp = false;
  float tempDt = 0;
  if (in.tcSampled) {
    lastTcMs_ = now;
    if (in.tc.ok) {
      tcFails_ = 0;
      tempDt = st_.tempValid ? (now - lastTcOkMs_) / 1000.0f : 0;
      lastTcOkMs_ = now;
      st_.tempC = in.tc.tempC;
      st_.tempValid = true;
      newTemp = true;
    } else if (++tcFails_ >= SAFETY_TC_FAIL_COUNT) {
      tcFails_ = SAFETY_TC_FAIL_COUNT;  // no wrap-around
      st_.tempValid = false;
      setFault("Temperature sensor: %s", in.tc.error ? in.tc.error : "read failed");
    }
  }

  const Sensors::PressureReading &pr = in.pressure;
  if (pr.ok) {
    // Smooth the pin voltage (pump pulsation), then convert: one smoothed value
    // feeds the PID, the dashboard and the IO Status page.
    pressPinMvFilt_ = pressPrimed_ ? pressPinMvFilt_ + 0.3f * (pr.pinMv - pressPinMvFilt_) : pr.pinMv;
    pressPrimed_ = true;
    pressScale_ = pr.supplyScale;
    pressBad_ = false;
  } else if (!pressBad_) {
    pressBad_ = true;
    pressBadSinceMs_ = now;
  }
  st_.pressureValid = pr.ok && pressPrimed_;
  st_.pressureBar = Sensors::pressureBarFromPinMv(pressPinMvFilt_, pressScale_);
  if (pr.supplyV > 0) st_.pressureSupplyV = pr.supplyV;
  // Out of range: show the live sample, not a stale smoothed value.
  st_.pressurePinMv = st_.pressureValid ? static_cast<uint16_t>(lroundf(pressPinMvFilt_)) : pr.pinMv;

  if (now - hzWindowStart_ >= 1000) {
    st_.mainsHz = (in.zeroCrossCount - hzZcStart_) / 2.0f * 1000.0f / (now - hzWindowStart_);
    hzZcStart_ = in.zeroCrossCount;
    hzWindowStart_ = now;
  }

  // --- Coffee switch arming / pump run-time limit ---
  if (!coffeeSw_) {
    coffeeArmed_ = true;
    st_.pumpTimedOut = false;
  }
  if (pumpRunning_ && now - pumpStartMs_ >= PUMP_MAX_RUN_MS) st_.pumpTimedOut = true;
  const bool coffeeActive = coffeeSw_ && coffeeArmed_ && !st_.pumpTimedOut;
  st_.waitSwitchCycle = coffeeSw_ && !coffeeActive;

  // --- state selection ---
  State next;
  if (in.inhibited) next = State::Inhibited;
  else if (faulted_) next = State::Fault;
  else if (steamSw_) next = coffeeActive ? State::HotWater : State::Steam;
  else next = coffeeActive ? State::Brewing : State::Idle;

  // --- state entry / exit ---
  if (next != st_.state) {
    if (next == State::Brewing) {
      pressPid_.reset();
      shotStartMs_ = now;
    }
    if (isPumpState(next) && !pumpRunning_) {
      pumpRunning_ = true;
      pumpStartMs_ = now;
    }
    if (!isPumpState(next)) pumpRunning_ = false;
    if (next == State::Fault || next == State::Inhibited) tempPid_.reset();
    if (next == State::Inhibited) coffeeArmed_ = false;  // no surprise brew if the update is aborted
    st_.state = next;
  }
  if (st_.state == State::Brewing) st_.shotMs = now - shotStartMs_;

  st_.setpointC = isSteamState(st_.state) ? cfg.steamTempC : cfg.coffeeTempC;
  st_.pressureTargetBar = st_.state == State::Brewing ? cfg.brewPressureBar : 0;

  // --- safety checks (only while controlling) ---
  if (st_.state != State::Fault && st_.state != State::Inhibited) {
    if (st_.tempValid && st_.tempC > SAFETY_MAX_TEMP_C) {
      setFault(st_.heaterDuty <= 0 ? "Boiler over-temperature (%.1f C) with heater off - heater SSR may be shorted"
                                   : "Boiler over-temperature (%.1f C)",
               st_.tempC);
    }
    if (pumpRunning_ && in.msSinceZeroCross > ZC_TIMEOUT_MS) {
      setFault("No zero-cross signal from the pump dimmer (check Z-C wiring / dimmer power)");
    }
    if (st_.state == State::Brewing && pressBad_ && now - pressBadSinceMs_ >= PRESSURE_FAULT_MS) {
      if (pr.error) setFault("Pressure sensor: %s", pr.error);
      else setFault("Pressure sensor out of range (%.2f V at sensor) - check wiring and 5 V supply", pr.sensorV);
    }
    // Heater-circuit check: hard heating with no pump flow must raise the temperature.
    if (st_.tempValid && st_.heaterDuty >= HEAT_CHECK_MIN_DUTY && !pumpRunning_) {
      if (!heatCheckActive_) {
        heatCheckActive_ = true;
        heatCheckStartMs_ = now;
        heatCheckStartTemp_ = st_.tempC;
      } else if (now - heatCheckStartMs_ >= HEAT_CHECK_WINDOW_MS) {
        if (st_.tempC - heatCheckStartTemp_ < HEAT_CHECK_MIN_RISE_C) {
          setFault("Boiler not heating (%.1f C after %lu s at full power) - check the 165 C high-limit "
                   "thermostat (manual reset), thermal fuse, heater and probe mounting",
                   st_.tempC, (unsigned long)(HEAT_CHECK_WINDOW_MS / 1000));
        }
        heatCheckStartMs_ = now;
        heatCheckStartTemp_ = st_.tempC;
      }
    } else {
      heatCheckActive_ = false;
    }
    if (faulted_) {
      st_.state = State::Fault;
      pumpRunning_ = false;
      tempPid_.reset();
    }
  }

  // --- outputs ---
  if (st_.state == State::Fault || st_.state == State::Inhibited) {
    st_.heaterDuty = 0;
    st_.pumpPower = 0;
    st_.heaterOn = st_.valveOn = false;
    const bool blink = st_.state == State::Fault && (now / 500) % 2;
    st_.coffeeLight = st_.steamLight = blink;
  } else {
    // Heater: PID on each new temperature sample, time-proportioned onto the SSR.
    tempPid_.setGains(cfg.tempKp, cfg.tempKi, cfg.tempKd);
    if (!st_.tempValid) {
      st_.heaterDuty = 0;
    } else if (newTemp) {
      st_.heaterDuty = tempPid_.update(st_.setpointC, st_.tempC, tempDt > 0 ? tempDt : TC_READ_PERIOD_MS / 1000.0f);
    }
    if (now - heaterWindowStart_ >= HEATER_WINDOW_MS) heaterWindowStart_ = now;
    st_.heaterOn = (now - heaterWindowStart_) < static_cast<uint32_t>(st_.heaterDuty * (HEATER_WINDOW_MS / 100.0f));

    // Pump and valve.
    if (st_.state == State::Brewing) {
      pressPid_.setGains(cfg.pressKp, cfg.pressKi, cfg.pressKd);
      st_.pumpPower = static_cast<uint8_t>(lroundf(pressPid_.update(cfg.brewPressureBar, st_.pressureBar, dtTick)));
    } else {
      st_.pumpPower = st_.state == State::HotWater ? 100 : 0;
    }
    st_.valveOn = st_.state == State::Brewing;

    // Ready lights: the active mode's light is on within +/- READY_BAND_C of its setpoint.
    const bool ready = st_.tempValid && fabsf(st_.tempC - st_.setpointC) <= READY_BAND_C;
    st_.coffeeLight = ready && !isSteamState(st_.state);
    st_.steamLight = ready && isSteamState(st_.state);
  }

  return Outputs{st_.heaterOn, st_.valveOn, st_.pumpPower, st_.coffeeLight, st_.steamLight};
}

}  // namespace Machine
