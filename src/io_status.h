#pragma once
#include <Arduino.h>

// Read-only status of the pins listed in include/io_pins.h.
// Pins are sampled from the main loop by reading registers only (no writes,
// no pinMode). A pin whose input buffer is off shows no level. The ADC is
// never touched here. The ADS1115 inputs (brew pressure, 5 V rail, spares) are
// reported from the control task's last conversions; this module never uses
// the I2C bus.
namespace IoStatus {

void begin();
void loop();         // samples pin states periodically
String json();       // {"pins":[...], "adc":[...]} for /api/io

}  // namespace IoStatus
