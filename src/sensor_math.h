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
  bool ok = false;           // false if the sensor voltage is outside its valid range, or on error
  float bar = 0;             // gauge pressure, clamped to >= 0
  float sensorV = 0;         // reconstructed sensor output voltage (uncorrected)
  uint16_t pinMv = 0;        // voltage at the divider node (ESP32 pin / ADS1115 AIN0)
  float supplyScale = 1.0f;  // ratiometric correction: nominal / measured supply (1 = none)
  float supplyV = 0;         // measured sensor supply, 0 if not measured
  const char *error = nullptr;  // converter failure (e.g. ADS1115 not responding)
};

// Divider + sensor scaling, clamped to >= 0. supplyScale from a PressureReading.
float pressureBarFromPinMv(float pinMv, float supplyScale = 1.0f);

// Native ADC: divider node voltage only.
PressureReading pressureFromPinMv(float pinMv);

// ADS1115: divider node voltage plus the 5 V rail through an identical divider
// (supplyPinMv <= 0: not measured yet -> no correction).
PressureReading pressureFromPinMv(float pinMv, float supplyPinMv);

// Converter failure: invalid reading carrying the reason.
PressureReading pressureError(const char *why);

}  // namespace Sensors
