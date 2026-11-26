#pragma once
#include <Arduino.h>

// Gaggia Guru machine control: state machine, heater/pump control and safety.
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
//   TEST       output test page has manual control (see testStart); safety checks still run
//
// The Coffee switch must be seen OFF before it can start the pump (after boot,
// and after the pump run-time limit stops it).
//
// The decisions live in machine_logic.cpp (unit-tested on the PC); this module
// only does the pin I/O, the FreeRTOS task, logging and publishing.
#include "machine_logic.h"  // State, Status, stateName(), Logic (hardware-free)

namespace Machine {

// Call first thing in setup(): drives every load output LOW.
void safeOutputs();

// Load settings, init sensors and start the control task.
void begin();

// Force all loads off and hold them off (firmware update, reboot). Safe to
// call from any task. release() resumes normal control (e.g. update failed).
void inhibit(const char *reason);
void release();

Status status();

// Output test page (manual control of the loads). testStart() checks that the
// machine is idle with both switches off and returns a session id (0 + error
// if not). The page must then call testUpdate() with that id at least every
// TEST_TIMEOUT_MS; it fails (error = why the test ended) once TEST is over.
// testStop() ends any session. Safe to call from any task.
uint32_t testStart(String &error);
bool testUpdate(uint32_t session, const TestCommand &cmd, String &error);
void testStop();

// Control tick execution time (wall clock, µs): average and worst over the last
// second, and worst since boot. The tick period is CONTROL_PERIOD_MS.
struct TickStats {
  uint32_t avgUs, maxUs, peakUs;
};
TickStats tickStats();
String json();  // status as JSON (for /api/machine and the WebSocket)

}  // namespace Machine
