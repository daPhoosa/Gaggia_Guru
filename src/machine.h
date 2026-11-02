#pragma once
#include <Arduino.h>

// Gaggia Savant machine control: state machine, heater/pump control and safety.
//
// Runs in its own FreeRTOS task (CONTROL_PERIOD_MS tick) watched by the task
// watchdog, independent of Wi-Fi, the web server and ArduinoOTA.
//
// Operating states (from the Coffee and Steam switches):
//   IDLE       coffee off, steam off : boiler -> coffee temp, pump off, valve closed
//   BREWING    coffee on,  steam off : boiler -> coffee temp, valve open, pump PID -> brew pressure
//   STEAM      coffee off, steam on  : boiler -> steam temp,  pump off, valve closed
//   HOT_WATER  coffee on,  steam on  : boiler -> steam temp,  pump 100 %, valve closed
// Overrides:
//   FAULT      latched until power cycle; all loads off, both lights blink
//   INHIBITED  firmware update / reboot in progress; all loads off
//
// The Coffee switch must be seen OFF before it can start the pump (after boot,
// and after the pump run-time limit stops it).
namespace Machine {

enum class State : uint8_t { Idle, Brewing, Steam, HotWater, Fault, Inhibited };

struct Status {
  State state;
  bool tempValid;
  float tempC;
  float setpointC;
  bool pressureValid;
  float pressureBar;
  float pressureTargetBar;  // 0 unless brewing
  uint16_t pressurePinMv;   // raw pin voltage of the last sample (for the IO Status page)
  float heaterDuty;         // 0-100 %
  uint8_t pumpPower;        // 0-100 %
  bool heaterOn, valveOn, coffeeLight, steamLight;
  bool coffeeSwitch, steamSwitch;
  bool waitSwitchCycle;     // Coffee switch is on but ignored until cycled off/on
  bool pumpTimedOut;
  uint32_t shotMs;          // current shot time, or last shot when idle
  float mainsHz;
  char fault[112];          // empty unless FAULT
  char inhibitReason[32];
};

// Call first thing in setup(): drives every load output LOW.
void safeOutputs();

// Load settings, init sensors and start the control task.
void begin();

// Force all loads off and hold them off (firmware update, reboot). Safe to
// call from any task. release() resumes normal control (e.g. update failed).
void inhibit(const char *reason);
void release();

Status status();
const char *stateName(State s);
String json();  // status as JSON (for /api/machine and the WebSocket)

}  // namespace Machine
