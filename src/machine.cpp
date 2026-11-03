#include "machine.h"

#include <ArduinoJson.h>
#include <esp_task_wdt.h>

#include "io_pins.h"
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
Machine::Logic logic;

void writeLoads(bool heater, bool valve, uint8_t pump, bool coffeeLight, bool steamLight) {
  digitalWrite(PIN_HEATER_SSR, heater);
  digitalWrite(PIN_VALVE_SSR, valve);
  Pump::setPower(pump);
  digitalWrite(PIN_COFFEE_LIGHT, coffeeLight);
  digitalWrite(PIN_STEAM_LIGHT, steamLight);
}

void allLoadsOff() { writeLoads(false, false, 0, false, false); }

// ---- one control tick: read hardware -> Logic::tick -> drive loads ----------
void tick() {
  const uint32_t now = millis();

  Machine::Inputs in{};
  in.nowMs = now;
  in.coffeeSwRaw = digitalRead(PIN_COFFEE_SW) == HIGH;
  in.steamSwRaw = digitalRead(PIN_STEAM_SW) == HIGH;
  in.tcSampled = logic.tcDue(now);
  if (in.tcSampled) in.tc = Sensors::readThermocouple();
  in.pressure = Sensors::readPressure();
  in.msSinceZeroCross = Pump::msSinceZeroCross();
  in.zeroCrossCount = Pump::zeroCrossCount();
  in.inhibited = inhibited;

  const State before = logic.status().state;
  const bool timedOutBefore = logic.status().pumpTimedOut;

  const Machine::Outputs out = logic.tick(in, Settings::get());
  writeLoads(out.heater, out.valve, out.pump, out.coffeeLight, out.steamLight);

  const Machine::Status &st = logic.status();
  if (st.state != before) {
    Serial.printf("[Machine] %s -> %s\n", Machine::stateName(before), Machine::stateName(st.state));
    if (st.state == State::Fault) Serial.printf("[Machine] FAULT: %s\n", st.fault);
  }
  if (st.pumpTimedOut && !timedOutBefore)
    Serial.println("[Machine] Pump run-time limit reached; cycle the Coffee switch");

  // --- publish ---
  portENTER_CRITICAL(&statusLock);
  shared = st;
  strlcpy(shared.inhibitReason, inhibitReason, sizeof(shared.inhibitReason));
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
  Pump::begin();

  const bool coffeeOn = digitalRead(PIN_COFFEE_SW) == HIGH;
  logic.begin(millis(), coffeeOn, digitalRead(PIN_STEAM_SW) == HIGH, Pump::zeroCrossCount());
  if (coffeeOn) Serial.println("[Machine] Coffee switch is ON at startup - turn it off to arm the pump");

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
