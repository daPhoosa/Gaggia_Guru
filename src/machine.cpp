#include "machine.h"

#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <math.h>

#include "io_pins.h"
#include "machine_config.h"
#include "pid.h"
#include "pump.h"
#include "sensors.h"
#include "settings.h"

namespace {

using Machine::State;

// ---- shared with other tasks ----------------------------------------------
portMUX_TYPE statusLock = portMUX_INITIALIZER_UNLOCKED;
Machine::Status shared{};               // published copy, guarded by statusLock
volatile bool inhibited = false;
char inhibitReason[32] = "";            // guarded by statusLock

// ---- control-task private state -------------------------------------------
Machine::Status st{};                   // working copy
Pid tempPid, pressPid;

bool faulted = false;
bool coffeeArmed = false;               // Coffee switch has been seen OFF
bool pumpRunning = false;
uint32_t pumpStartMs = 0;
uint32_t shotStartMs = 0;

// switch debounce
bool coffeeSw = false, steamSw = false;
uint8_t coffeeCnt = 0, steamCnt = 0;

// sensors
uint32_t lastTcMs = 0, lastTcOkMs = 0;
uint8_t tcFails = 0;
float pressPinMvFilt = 0;                // EMA-smoothed pin voltage (mV)
bool pressPrimed = false;
uint32_t pressBadSinceMs = 0;
bool pressBad = false;

// heater
uint32_t heaterWindowStart = 0;
bool heatCheckActive = false;
uint32_t heatCheckStartMs = 0;
float heatCheckStartTemp = 0;

// mains frequency
uint32_t hzWindowStart = 0, hzZcStart = 0;

bool isPumpState(State s) { return s == State::Brewing || s == State::HotWater; }
bool isSteamState(State s) { return s == State::Steam || s == State::HotWater; }

void writeLoads(bool heater, bool valve, uint8_t pump, bool coffeeLight, bool steamLight) {
  digitalWrite(PIN_HEATER_SSR, heater);
  digitalWrite(PIN_VALVE_SSR, valve);
  Pump::setPower(pump);
  digitalWrite(PIN_COFFEE_LIGHT, coffeeLight);
  digitalWrite(PIN_STEAM_LIGHT, steamLight);
}

void allLoadsOff() { writeLoads(false, false, 0, false, false); }

void setFault(const char *fmt, ...) {
  if (faulted) return;  // keep the first cause
  faulted = true;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(st.fault, sizeof(st.fault), fmt, ap);
  va_end(ap);
  allLoadsOff();
  Serial.printf("[Machine] FAULT: %s\n", st.fault);
}

bool debounce(int pin, bool &state, uint8_t &cnt) {
  bool raw = digitalRead(pin) == HIGH;
  if (raw == state) {
    cnt = 0;
  } else if (++cnt >= SWITCH_DEBOUNCE_TICKS) {
    state = raw;
    cnt = 0;
  }
  return state;
}

// ---- one control tick ------------------------------------------------------
void tick() {
  const uint32_t now = millis();
  const MachineSettings cfg = Settings::get();
  constexpr float dtTick = CONTROL_PERIOD_MS / 1000.0f;

  // --- inputs ---
  debounce(PIN_COFFEE_SW, coffeeSw, coffeeCnt);
  debounce(PIN_STEAM_SW, steamSw, steamCnt);
  st.coffeeSwitch = coffeeSw;
  st.steamSwitch = steamSw;

  bool newTemp = false;
  float tempDt = 0;
  if (now - lastTcMs >= TC_READ_PERIOD_MS) {
    lastTcMs = now;
    Sensors::TcReading tc = Sensors::readThermocouple();
    if (tc.ok) {
      tcFails = 0;
      tempDt = st.tempValid ? (now - lastTcOkMs) / 1000.0f : 0;
      lastTcOkMs = now;
      st.tempC = tc.tempC;
      st.tempValid = true;
      newTemp = true;
    } else if (++tcFails >= SAFETY_TC_FAIL_COUNT) {
      st.tempValid = false;
      setFault("Temperature sensor: %s", tc.error);
    }
  }

  Sensors::PressureReading pr = Sensors::readPressure();
  if (pr.ok) {
    // Smooth the pin voltage (pump pulsation), then convert: one smoothed value
    // feeds the PID, the dashboard and the IO Status page.
    pressPinMvFilt = pressPrimed ? pressPinMvFilt + 0.3f * (pr.pinMv - pressPinMvFilt) : pr.pinMv;
    pressPrimed = true;
    pressBad = false;
  } else if (!pressBad) {
    pressBad = true;
    pressBadSinceMs = now;
  }
  st.pressureValid = pr.ok && pressPrimed;
  st.pressureBar = Sensors::pressureBarFromPinMv(pressPinMvFilt);
  // Out of range: show the live sample, not a stale smoothed value.
  st.pressurePinMv = st.pressureValid ? static_cast<uint16_t>(lroundf(pressPinMvFilt)) : pr.pinMv;

  if (now - hzWindowStart >= 1000) {
    uint32_t zc = Pump::zeroCrossCount();
    st.mainsHz = (zc - hzZcStart) / 2.0f * 1000.0f / (now - hzWindowStart);
    hzZcStart = zc;
    hzWindowStart = now;
  }

  // --- Coffee switch arming / pump run-time limit ---
  if (!coffeeSw) {
    coffeeArmed = true;
    st.pumpTimedOut = false;
  }
  if (pumpRunning && now - pumpStartMs >= PUMP_MAX_RUN_MS && !st.pumpTimedOut) {
    st.pumpTimedOut = true;
    Serial.println("[Machine] Pump run-time limit reached; cycle the Coffee switch");
  }
  const bool coffeeActive = coffeeSw && coffeeArmed && !st.pumpTimedOut;
  st.waitSwitchCycle = coffeeSw && !coffeeActive;

  // --- state selection ---
  State next;
  if (inhibited) next = State::Inhibited;
  else if (faulted) next = State::Fault;
  else if (steamSw) next = coffeeActive ? State::HotWater : State::Steam;
  else next = coffeeActive ? State::Brewing : State::Idle;

  // --- state entry / exit ---
  if (next != st.state) {
    Serial.printf("[Machine] %s -> %s\n", Machine::stateName(st.state), Machine::stateName(next));
    if (next == State::Brewing) {
      pressPid.reset();
      shotStartMs = now;
    }
    if (isPumpState(next) && !pumpRunning) {
      pumpRunning = true;
      pumpStartMs = now;
    }
    if (!isPumpState(next)) pumpRunning = false;
    if (next == State::Fault || next == State::Inhibited) tempPid.reset();
    if (next == State::Inhibited) coffeeArmed = false;  // no surprise brew if the update is aborted
    st.state = next;
  }
  if (st.state == State::Brewing) st.shotMs = now - shotStartMs;

  st.setpointC = isSteamState(st.state) ? cfg.steamTempC : cfg.coffeeTempC;
  st.pressureTargetBar = st.state == State::Brewing ? cfg.brewPressureBar : 0;

  // --- safety checks (only while controlling) ---
  if (st.state != State::Fault && st.state != State::Inhibited) {
    if (st.tempValid && st.tempC > SAFETY_MAX_TEMP_C) {
      setFault(st.heaterDuty <= 0 ? "Boiler over-temperature (%.1f C) with heater off - heater SSR may be shorted"
                                  : "Boiler over-temperature (%.1f C)",
               st.tempC);
    }
    if (pumpRunning && Pump::msSinceZeroCross() > ZC_TIMEOUT_MS) {
      setFault("No zero-cross signal from the pump dimmer (check Z-C wiring / dimmer power)");
    }
    if (st.state == State::Brewing && pressBad && now - pressBadSinceMs >= PRESSURE_FAULT_MS) {
      setFault("Pressure sensor out of range (%.2f V at sensor) - check wiring and 5 V supply", pr.sensorV);
    }
    // Heater-circuit check: hard heating with no pump flow must raise the temperature.
    if (st.tempValid && st.heaterDuty >= HEAT_CHECK_MIN_DUTY && !pumpRunning) {
      if (!heatCheckActive) {
        heatCheckActive = true;
        heatCheckStartMs = now;
        heatCheckStartTemp = st.tempC;
      } else if (now - heatCheckStartMs >= HEAT_CHECK_WINDOW_MS) {
        if (st.tempC - heatCheckStartTemp < HEAT_CHECK_MIN_RISE_C) {
          setFault("Boiler not heating (%.1f C after %lu s at full power) - check the 165 C high-limit "
                   "thermostat (manual reset), thermal fuse, heater and probe mounting",
                   st.tempC, (unsigned long)(HEAT_CHECK_WINDOW_MS / 1000));
        }
        heatCheckStartMs = now;
        heatCheckStartTemp = st.tempC;
      }
    } else {
      heatCheckActive = false;
    }
    if (faulted) st.state = State::Fault;
  }

  // --- outputs ---
  if (st.state == State::Fault || st.state == State::Inhibited) {
    st.heaterDuty = 0;
    st.pumpPower = 0;
    st.heaterOn = st.valveOn = false;
    const bool blink = st.state == State::Fault && (now / 500) % 2;
    st.coffeeLight = st.steamLight = blink;
    writeLoads(false, false, 0, blink, blink);
  } else {
    // Heater: PID on each new temperature sample, time-proportioned onto the SSR.
    tempPid.setGains(cfg.tempKp, cfg.tempKi, cfg.tempKd);
    if (!st.tempValid) {
      st.heaterDuty = 0;
    } else if (newTemp) {
      st.heaterDuty = tempPid.update(st.setpointC, st.tempC, tempDt > 0 ? tempDt : TC_READ_PERIOD_MS / 1000.0f);
    }
    if (now - heaterWindowStart >= HEATER_WINDOW_MS) heaterWindowStart = now;
    st.heaterOn = (now - heaterWindowStart) < static_cast<uint32_t>(st.heaterDuty * (HEATER_WINDOW_MS / 100.0f));

    // Pump and valve.
    if (st.state == State::Brewing) {
      pressPid.setGains(cfg.pressKp, cfg.pressKi, cfg.pressKd);
      st.pumpPower = static_cast<uint8_t>(lroundf(pressPid.update(cfg.brewPressureBar, st.pressureBar, dtTick)));
    } else {
      st.pumpPower = st.state == State::HotWater ? 100 : 0;
    }
    st.valveOn = st.state == State::Brewing;

    // Ready lights: the active mode's light is on within +/- READY_BAND_C of its setpoint.
    const bool ready = st.tempValid && fabsf(st.tempC - st.setpointC) <= READY_BAND_C;
    st.coffeeLight = ready && !isSteamState(st.state);
    st.steamLight = ready && isSteamState(st.state);

    writeLoads(st.heaterOn, st.valveOn, st.pumpPower, st.coffeeLight, st.steamLight);
  }

  // --- publish ---
  portENTER_CRITICAL(&statusLock);
  strlcpy(st.inhibitReason, inhibitReason, sizeof(st.inhibitReason));
  shared = st;
  portEXIT_CRITICAL(&statusLock);
}

void controlTask(void *) {
  esp_task_wdt_add(nullptr);
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    esp_task_wdt_reset();
    tick();
  }
}

}  // namespace

namespace Machine {

void safeOutputs() {
  const int pins[] = {PIN_HEATER_SSR, PIN_VALVE_SSR, PIN_PUMP_PSM, PIN_COFFEE_LIGHT, PIN_STEAM_LIGHT};
  for (int p : pins) {
    digitalWrite(p, LOW);
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
}

void begin() {
  safeOutputs();
  Settings::begin();
  Sensors::beginThermocouple();
  Sensors::beginPressure();
  pinMode(PIN_COFFEE_SW, INPUT_PULLDOWN);
  pinMode(PIN_STEAM_SW, INPUT_PULLDOWN);
  coffeeSw = digitalRead(PIN_COFFEE_SW) == HIGH;  // start from the real positions
  steamSw = digitalRead(PIN_STEAM_SW) == HIGH;
  coffeeArmed = false;                            // must see Coffee OFF before brewing
  Pump::begin();

  st.state = State::Idle;
  tempPid.setOutputLimits(0, 100);
  pressPid.setOutputLimits(0, 100);
  heaterWindowStart = hzWindowStart = millis();
  hzZcStart = Pump::zeroCrossCount();

  if (coffeeSw) Serial.println("[Machine] Coffee switch is ON at startup - turn it off to arm the pump");

  esp_task_wdt_init(CONTROL_WDT_TIMEOUT_S, true);  // panic + reboot if the control task stalls
  xTaskCreatePinnedToCore(controlTask, "control", 4096, nullptr, 5, nullptr, 1);
  Serial.println("[Machine] Control task started");
}

void inhibit(const char *reason) {
  portENTER_CRITICAL(&statusLock);
  strlcpy(inhibitReason, reason ? reason : "", sizeof(inhibitReason));
  portEXIT_CRITICAL(&statusLock);
  inhibited = true;
  allLoadsOff();  // immediately; the control task keeps them off from its next tick
  Serial.printf("[Machine] Loads inhibited: %s\n", reason ? reason : "");
}

void release() {
  inhibited = false;
  portENTER_CRITICAL(&statusLock);
  inhibitReason[0] = '\0';
  portEXIT_CRITICAL(&statusLock);
  Serial.println("[Machine] Loads released");
}

Status status() {
  portENTER_CRITICAL(&statusLock);
  Status s = shared;
  portEXIT_CRITICAL(&statusLock);
  return s;
}

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

String json() {
  Status s = status();
  JsonDocument doc;
  doc["type"] = "machine";
  doc["state"] = stateName(s.state);
  if (s.tempValid) doc["temp"] = serialized(String(s.tempC, 2));
  else doc["temp"] = nullptr;
  doc["setpoint"] = s.setpointC;
  if (s.pressureValid) doc["pressure"] = serialized(String(s.pressureBar, 2));
  else doc["pressure"] = nullptr;
  doc["pressureTarget"] = s.pressureTargetBar;
  doc["heaterDuty"] = serialized(String(s.heaterDuty, 1));
  doc["pump"] = s.pumpPower;
  doc["heater"] = s.heaterOn;
  doc["valve"] = s.valveOn;
  doc["coffeeLight"] = s.coffeeLight;
  doc["steamLight"] = s.steamLight;
  doc["coffeeSw"] = s.coffeeSwitch;
  doc["steamSw"] = s.steamSwitch;
  doc["waitSwitchCycle"] = s.waitSwitchCycle;
  doc["pumpTimedOut"] = s.pumpTimedOut;
  doc["shotMs"] = s.shotMs;
  doc["mainsHz"] = serialized(String(s.mainsHz, 1));
  if (s.fault[0]) doc["fault"] = s.fault;
  if (s.inhibitReason[0]) doc["inhibit"] = s.inhibitReason;
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace Machine
