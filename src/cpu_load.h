#pragma once
#include <Arduino.h>

// Estimated load on each CPU core, for the dashboard's Device status card.
//
// The precompiled Arduino core has FreeRTOS run-time stats turned off, so this
// counts idle time instead: a hook in each core's idle task counts loops, and
// load = 1 - (loops per second / most loops per second seen since boot).
// The hook keeps the idle task spinning instead of sleeping until the next
// interrupt (a few tens of mA more; no effect on timing). If a core is never
// fully idle, the baseline is low and its load reads low. Time in interrupts
// counts as busy.
namespace CpuLoad {

void begin();  // register the idle hooks and start the 1 s sampling timer

// Load of core 0 / 1 in percent over the last second, or -1 until the first
// second has been measured.
float percent(int core);

}  // namespace CpuLoad
