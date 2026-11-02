#pragma once
#include <Arduino.h>

// Read-only status of the pins listed in include/io_pins.h.
// Pins are sampled from the main loop without reconfiguring them; the only
// exception is pins typed "Analog", which are read with analogRead().
namespace IoStatus {

void begin();
void loop();         // samples pin states periodically
String json();       // {"pins":[...]} for /api/io

}  // namespace IoStatus
