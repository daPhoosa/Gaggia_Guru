#pragma once
// Hardware-free sensor decoding (no Arduino headers): unit-tested on the PC
// (pio test -e native). sensors.cpp does the pin I/O and calls these.
#include <stdint.h>

namespace Sensors {

// ---- MAX31855K thermocouple ------------------------------------------------
struct TcReading {
  bool ok;              // false on fault, bus error or implausible value
  float tempC;          // thermocouple temperature (valid when ok)
  float coldJunctionC;
  const char *error;    // reason when !ok
};

// Decode one 32-bit MAX31855 frame (MSB first):
//   D31..D18  thermocouple temp, signed 14-bit, 0.25 C/LSB
//   D16       fault (any)
//   D15..D4   cold-junction temp, signed 12-bit, 0.0625 C/LSB
//   D2 SCV (short to VCC), D1 SCG (short to GND), D0 OC (open circuit)
TcReading decodeMax31855(uint32_t frame);

// ---- XDB401 pressure sensor via divider -------------------------------------
struct PressureReading {
  bool ok;          // false if the sensor voltage is outside its valid range
  float bar;        // gauge pressure, clamped to >= 0
  float sensorV;    // reconstructed sensor output voltage
  uint16_t pinMv;   // voltage at the ESP32 pin (after the divider)
};

float pressureBarFromPinMv(float pinMv);       // divider + sensor scaling, clamped to >= 0
PressureReading pressureFromPinMv(float pinMv);  // full reading incl. range check

}  // namespace Sensors
