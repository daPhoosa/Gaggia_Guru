#include "sensors.h"

#include "io_pins.h"
#include "machine_config.h"

#include <Wire.h>

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
// Pressure: the ADS1115 reads the divider node (pump pulsation is filtered
// further by the caller), then sensor_math.cpp undoes the divider and maps
// 0.5-4.5 V to bar.
// ---------------------------------------------------------------------------

// ADS1115 on the I2C bus, single-shot conversions. Only the control task uses
// the bus, so no locking is needed.
namespace {
constexpr uint8_t kRegConversion = 0x00, kRegConfig = 0x01;
// OS=1 (start) | MUX=1xx (AINx vs GND) | PGA=001 (+-4.096 V, 125 uV/LSB) |
// MODE=1 (single-shot) | DR=110 (475 SPS, ~2.1 ms) | COMP_QUE=11 (comparator off)
constexpr uint16_t kConfigBase = 0x8000 | (1u << 9) | 0x0100 | (0b110u << 5) | 0x0003;
constexpr float kMvPerLsb = 0.125f;
constexpr uint32_t kConvTimeoutMs = 10;

float supplyPinMv = 0;  // last 5 V rail measurement at AIN1 (divided), 0 = none yet
uint32_t lastSupplyMs = 0;

bool adsWrite(uint8_t reg, uint16_t v) {
  Wire.beginTransmission(ADS1115_I2C_ADDR);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(v >> 8));
  Wire.write(static_cast<uint8_t>(v & 0xFF));
  return Wire.endTransmission() == 0;
}

bool adsRead(uint8_t reg, uint16_t &v) {
  Wire.beginTransmission(ADS1115_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<uint8_t>(ADS1115_I2C_ADDR), static_cast<uint8_t>(2)) != 2) return false;
  const uint8_t hi = Wire.read(), lo = Wire.read();
  v = static_cast<uint16_t>(hi << 8 | lo);
  return true;
}

// One single-ended conversion on AIN0-3; result in millivolts at the pin.
bool adsConvert(uint8_t channel, float &mv) {
  if (!adsWrite(kRegConfig, kConfigBase | static_cast<uint16_t>((4u + channel) << 12))) return false;
  const uint32_t start = millis();
  uint16_t cfg = 0;
  do {
    delay(1);  // yields; conversion takes ~2.1 ms
    if (!adsRead(kRegConfig, cfg)) return false;
    if (millis() - start > kConvTimeoutMs) return false;
  } while (!(cfg & 0x8000));  // OS bit reads 1 when the conversion is done
  uint16_t raw = 0;
  if (!adsRead(kRegConversion, raw)) return false;
  mv = static_cast<int16_t>(raw) * kMvPerLsb;
  return true;
}
}  // namespace

void beginPressure() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  Wire.setTimeOut(5);  // ms; a missing chip must not stall the control task
  uint16_t cfg = 0;
  Serial.println(adsRead(kRegConfig, cfg) ? "[Pressure] ADS1115 found"
                                          : "[Pressure] ADS1115 not responding - pressure invalid");
}

PressureReading readPressure() {
  // The 5 V rail changes slowly: re-measure it about once a second.
  if (supplyPinMv <= 0 || millis() - lastSupplyMs >= PRESSURE_SUPPLY_PERIOD_MS) {
    float mv;
    if (adsConvert(1, mv)) {
      supplyPinMv = mv;
      lastSupplyMs = millis();
    }
  }
  float mv;
  if (!adsConvert(0, mv)) return pressureError("ADS1115 not responding (check I2C wiring / 3.3 V)");
  return pressureFromPinMv(mv, supplyPinMv);
}

}  // namespace Sensors
