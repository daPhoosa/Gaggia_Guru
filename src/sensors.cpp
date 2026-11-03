#include "sensors.h"

#include "io_pins.h"
#include "machine_config.h"

namespace Sensors {

// ---------------------------------------------------------------------------
// MAX31855K: 32-bit frame, MSB first, data valid while SCK is low.
// Frame layout and decoding: sensor_math.cpp.
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

TcReading readThermocouple() { return decodeMax31855(readFrame()); }

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
  return pressureFromPinMv(mv / static_cast<float>(kSamples));
}

}  // namespace Sensors
