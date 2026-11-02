#include "pump.h"

#include <soc/gpio_struct.h>

#include "io_pins.h"

static_assert(PIN_PUMP_PSM < 32, "ISR uses GPIO.out_w1ts/w1tc, which cover GPIO0-31 only");

namespace {

// Edges closer than this are noise / the second edge of one Z-C pulse.
// Half-cycle is 8.33 ms at 60 Hz (10 ms at 50 Hz).
constexpr uint32_t kMinZcGapUs = 4000;

volatile uint8_t targetPower = 0;  // written by control task, read by ISR
volatile uint32_t lastZcUs = 0;
volatile uint32_t zcCount = 0;
uint8_t halfCycle = 0;  // ISR-only
uint8_t acc = 0;        // ISR-only Bresenham accumulator

inline void IRAM_ATTR gateOn() { GPIO.out_w1ts = 1u << PIN_PUMP_PSM; }
inline void IRAM_ATTR gateOff() { GPIO.out_w1tc = 1u << PIN_PUMP_PSM; }

void IRAM_ATTR onZeroCross() {
  const uint32_t now = micros();
  if (now - lastZcUs < kMinZcGapUs) return;
  lastZcUs = now;
  zcCount = zcCount + 1;

  halfCycle ^= 1;
  if (halfCycle) return;  // decide once per full cycle, hold for both halves

  const uint8_t p = targetPower;
  acc += p;
  if (acc >= 100) {
    acc -= 100;
    gateOn();
  } else {
    gateOff();
  }
}

}  // namespace

namespace Pump {

void begin() {
  pinMode(PIN_PUMP_PSM, OUTPUT);
  digitalWrite(PIN_PUMP_PSM, LOW);
  pinMode(PIN_PUMP_ZC, INPUT);  // RobotDyn Z-C output has its own pull-up (to 3.3 V VCC)
  lastZcUs = micros() - 1000000UL;
  attachInterrupt(digitalPinToInterrupt(PIN_PUMP_ZC), onZeroCross, RISING);
}

void setPower(uint8_t percent) {
  if (percent > 100) percent = 100;
  targetPower = percent;
  if (percent == 0) gateOff();
}

uint8_t power() { return targetPower; }

uint32_t msSinceZeroCross() { return (micros() - lastZcUs) / 1000; }

uint32_t zeroCrossCount() { return zcCount; }

}  // namespace Pump
