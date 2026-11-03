#include "sensor_math.h"

#include <math.h>

#include "machine_config.h"

namespace Sensors {

TcReading decodeMax31855(uint32_t v) {
  TcReading r{false, NAN, NAN, nullptr};

  if (v == 0 || v == 0xFFFFFFFFu) {  // SO stuck low/high: chip missing or unpowered
    r.error = "thermocouple chip not responding";
    return r;
  }
  if (v & (1u << 16)) {
    r.error = (v & 0x1)   ? "thermocouple open circuit"
              : (v & 0x2) ? "thermocouple shorted to GND"
              : (v & 0x4) ? "thermocouple shorted to VCC"
                          : "thermocouple fault";
    return r;
  }

  const int16_t tc = static_cast<int16_t>((v >> 16) & 0xFFFC) >> 2;  // sign-extend 14-bit
  const int16_t cj = static_cast<int16_t>(v & 0xFFF0) >> 4;          // sign-extend 12-bit
  r.tempC = tc * 0.25f;
  r.coldJunctionC = cj * 0.0625f;

  if (r.tempC < SAFETY_MIN_VALID_TEMP_C || r.tempC > SAFETY_MAX_VALID_TEMP_C) {
    r.error = "thermocouple reading implausible";
    return r;
  }
  r.ok = true;
  return r;
}

float pressureBarFromPinMv(float pinMv) {
  const float sensorV = pinMv / 1000.0f / PRESSURE_DIVIDER_RATIO;
  const float bar = (sensorV - PRESSURE_V_MIN) / (PRESSURE_V_MAX - PRESSURE_V_MIN) * PRESSURE_FULL_SCALE_BAR;
  return bar < 0 ? 0 : bar;
}

PressureReading pressureFromPinMv(float pinMv) {
  PressureReading r;
  r.pinMv = static_cast<uint16_t>(pinMv < 0 ? 0 : pinMv > 65535 ? 65535 : pinMv);
  r.sensorV = pinMv / 1000.0f / PRESSURE_DIVIDER_RATIO;
  r.ok = r.sensorV >= PRESSURE_V_VALID_LOW && r.sensorV <= PRESSURE_V_VALID_HIGH;
  r.bar = pressureBarFromPinMv(pinMv);
  return r;
}

}  // namespace Sensors
