#pragma once
#include <Arduino.h>

// Vibratory pump via RobotDyn AC dimmer, pulse-skip modulation.
//
// The Z-C interrupt fires at every mains zero crossing. At the start of each
// full mains cycle the ISR decides (Bresenham-style) whether the triac gate
// (PSM pin) is held on for that whole cycle, so power N % = N of every 100
// cycles, evenly spread. Whole cycles are used because the pump's internal
// diode only conducts on one half-cycle.
namespace Pump {

void begin();
void setPower(uint8_t percent);  // 0-100; 0 also forces the gate low immediately
uint8_t power();
uint32_t msSinceZeroCross();     // large if the Z-C signal is missing
uint32_t zeroCrossCount();       // total Z-C edges (for mains frequency display)

}  // namespace Pump
