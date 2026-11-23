#pragma once
#include <Arduino.h>

#include "sensor_math.h"  // TcReading, PressureReading, decoding/conversion (hardware-free)

// Sensor drivers used only by the control task (not thread-safe on their own).
namespace Sensors {

// MAX31855K thermocouple (hardware VSPI bus, read-only; bus-sharing rules in sensors.cpp)
void beginThermocouple();
TcReading readThermocouple();

// XDB401 pressure sensor via divider, read by the ADS1115 (I2C)
void beginPressure();
PressureReading readPressure();

// Last ADS1115 conversion on each input (AIN0 pressure divider, AIN1 5 V rail
// divider, AIN2 / AIN3 spare), for the IO Status page. The control task
// records them; adsSamples() copies them safely from any task.
constexpr uint8_t kAdsChannels = 4;
struct AdsSample {
  bool valid = false;  // false until first read, or if the last read failed
  int16_t raw = 0;     // conversion code, 125 uV/LSB (+-4.096 V range)
  float mv = 0;        // voltage at the pin
  uint32_t atMs = 0;   // millis() of the last attempt
};
void adsSamples(AdsSample (&out)[kAdsChannels]);

// Read one spare input (AIN2 / AIN3, alternating) when ADS_SPARE_PERIOD_MS has
// passed. Control task only. The firmware does not use these values; they are
// only shown on the IO Status page.
void sampleSpareInputs();

}  // namespace Sensors
