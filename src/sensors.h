#pragma once
#include <Arduino.h>

#include "sensor_math.h"  // TcReading, PressureReading, decoding/conversion (hardware-free)

// Sensor drivers used only by the control task (not thread-safe on their own).
namespace Sensors {

// MAX31855K thermocouple (bit-banged SPI, read-only)
void beginThermocouple();
TcReading readThermocouple();

// XDB401 pressure sensor via divider, read by the ADS1115 (I2C)
void beginPressure();
PressureReading readPressure();

}  // namespace Sensors
