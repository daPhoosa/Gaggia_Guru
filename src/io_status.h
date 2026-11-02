#pragma once
#include <Arduino.h>

// Read-only status of the pins listed in include/io_pins.h.
// Pins are sampled from the main loop without reconfiguring them. The ADC is
// never touched here: analog values come from the module that owns the pin
// (the pressure input is read only by the machine control task).
namespace IoStatus {

void begin();
void loop();         // samples pin states periodically
String json();       // {"pins":[...]} for /api/io

}  // namespace IoStatus
