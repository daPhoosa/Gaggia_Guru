#include "sensors.h"

#include "io_pins.h"
#include "machine_config.h"

namespace Sensors {

// ---------------------------------------------------------------------------
// MAX31855K: 32-bit frame, MSB first, data valid while SCK is low.
//   D31..D18  thermocouple temp, signed 14-bit, 0.25 C/LSB
//   D16       fault (any)
//   D15..D4   cold-junction temp, signed 12-bit, 0.0625 C/LSB
//   D2 SCV (short to VCC), D1 SCG (short to GND), D0 OC (open circuit)
// ---------------------------------------------------------------------------
void beginThermocouple() {
  pinMode(PIN_TC_CS, OUTPUT);
  digitalWrite(PIN_TC_CS, HIGH);
  pinMode(PIN_TC_SCK, OUTPUT);
  digitalWrite(PIN_TC_SCK, LOW);
  pinMode(PIN_TC_SO, INPUT);
}

static uint32_t readFrame() {
  uint32_t v = 0;
  digitalWrite(PIN_TC_SCK, LOW);
  digitalWrite(PIN_TC_CS, LOW);
  delayMicroseconds(2);
  for (int i = 0; i < 32; ++i) {
    digitalWrite(PIN_TC_SCK, LOW);
    delayMicroseconds(2);
    v = (v << 1) | (digitalRead(PIN_TC_SO) ? 1u : 0u);
    digitalWrite(PIN_TC_SCK, HIGH);
    delayMicroseconds(2);
  }
  digitalWrite(PIN_TC_SCK, LOW);
  digitalWrite(PIN_TC_CS, HIGH);
  return v;
}

TcReading readThermocouple() {
  TcReading r{false, NAN, NAN, nullptr};
  const uint32_t v = readFrame();

  if (v == 0 || v == 0xFFFFFFFFu) {  // SO stuck low/high: chip missing or unpowered
    r.error = "thermocouple chip not responding";
    return r;
  }
  if (v & (1u << 16)) {
    r.error = (v & 0x1) ? "thermocouple open circuit"
              : (v & 0x2) ? "thermocouple shorted to GND"
              : (v & 0x4) ? "thermocouple shorted to VCC"
                          : "thermocouple fault";
    return r;
  }

  int16_t tc = static_cast<int16_t>((v >> 16) & 0xFFFC) >> 2;  // sign-extend 14-bit
  int16_t cj = static_cast<int16_t>(v & 0xFFF0) >> 4;          // sign-extend 12-bit
  r.tempC = tc * 0.25f;
  r.coldJunctionC = cj * 0.0625f;

  if (r.tempC < SAFETY_MIN_VALID_TEMP_C || r.tempC > SAFETY_MAX_VALID_TEMP_C) {
    r.error = "thermocouple reading implausible";
    return r;
  }
  r.ok = true;
  return r;
}

// ---------------------------------------------------------------------------
// Pressure: average a few ADC samples (pump pulsation is filtered further by
// the caller), undo the divider, map 0.5-4.5 V to 0-full scale.
// ---------------------------------------------------------------------------
void beginPressure() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_PRESSURE, ADC_11db);  // ~0.15-2.45 V usable, linear
}

PressureReading readPressure() {
  constexpr int kSamples = 8;
  uint32_t mv = 0;
  for (int i = 0; i < kSamples; ++i) mv += analogReadMilliVolts(PIN_PRESSURE);
  const float pinV = (mv / static_cast<float>(kSamples)) / 1000.0f;

  PressureReading r;
  r.pinMv = static_cast<uint16_t>(mv / kSamples);
  r.sensorV = pinV / PRESSURE_DIVIDER_RATIO;
  r.ok = r.sensorV >= PRESSURE_V_VALID_LOW && r.sensorV <= PRESSURE_V_VALID_HIGH;
  float bar = (r.sensorV - PRESSURE_V_MIN) / (PRESSURE_V_MAX - PRESSURE_V_MIN) * PRESSURE_FULL_SCALE_BAR;
  r.bar = bar < 0 ? 0 : bar;
  return r;
}

}  // namespace Sensors
