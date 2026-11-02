#pragma once
#include <Arduino.h>

// Sensor drivers used only by the control task (not thread-safe on their own).
namespace Sensors {

// ---- MAX31855K thermocouple (bit-banged SPI, read-only) -------------------
struct TcReading {
  bool ok;          // false on fault, bus error or implausible value
  float tempC;      // thermocouple temperature (valid when ok)
  float coldJunctionC;
  const char *error;  // reason when !ok
};
void beginThermocouple();
TcReading readThermocouple();

// ---- XDB401 pressure sensor via divider on an ADC1 pin --------------------
struct PressureReading {
  bool ok;          // false if the sensor voltage is outside its valid range
  float bar;        // gauge pressure, clamped to >= 0 (valid when ok)
  float sensorV;    // reconstructed sensor output voltage
  uint16_t pinMv;   // averaged voltage at the ESP32 pin (after the divider)
};
void beginPressure();
PressureReading readPressure();

}  // namespace Sensors
