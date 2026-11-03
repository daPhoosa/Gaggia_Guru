#include "io_status.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_struct.h>
#include <soc/io_mux_reg.h>
#include <soc/rtc_io_periph.h>

#include "io_pins.h"
#include "machine.h"

// Raw register access. (The ESP-IDF REG_* macros contain a compile-time assert
// that warns when expanded inside an anonymous namespace.)
static inline uint32_t regRead(uint32_t addr) { return *reinterpret_cast<volatile uint32_t *>(addr); }

namespace {

enum class IoType : uint8_t { Analog, Digital, Communications, Unused };

struct IoPinInfo {
  uint8_t gpio;
  const char *macro;
  const char *name;
  IoType type;
  const char *notes;
};

#include "io_pins_table.gen.h"  // generated from include/io_pins.h

const IoPinInfo kPins[] = {IO_PIN_TABLE};
constexpr size_t kPinCount = sizeof(kPins) / sizeof(kPins[0]);

enum class Mode : uint8_t { Input, Output, Peripheral, Analog };

struct PinState {
  Mode mode;
  int8_t level;      // 0/1, or -1 if unknown
  int8_t pull;       // 1 = pull-up, -1 = pull-down, 0 = none
  int16_t mv;        // ADC millivolts (Analog pins), -1 if n/a
  float bar;         // pressure pin: smoothed pressure in bar, NAN if n/a
  const char *note;  // runtime note (e.g. ADC2 + Wi-Fi), or nullptr
};

PinState states[kPinCount];
uint32_t lastSample = 0;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

const char *typeName(IoType t) {
  switch (t) {
    case IoType::Analog: return "Analog";
    case IoType::Digital: return "Digital";
    case IoType::Communications: return "Communications";
    default: return "Unused";
  }
}
const char *modeName(Mode m) {
  switch (m) {
    case Mode::Output: return "OUT";
    case Mode::Peripheral: return "PERIPH";
    case Mode::Analog: return "ANALOG";
    default: return "IN";
  }
}

bool isAdc1(uint8_t g) { return g >= 32 && g <= 39; }
bool isAdc2(uint8_t g) {
  return g == 0 || g == 2 || g == 4 || (g >= 12 && g <= 15) || (g >= 25 && g <= 27);
}

bool outputEnabled(uint8_t g) {
  return g < 32 ? (GPIO.enable >> g) & 1 : (GPIO.enable1.val >> (g - 32)) & 1;
}
int outputLevel(uint8_t g) { return g < 32 ? (GPIO.out >> g) & 1 : (GPIO.out1.val >> (g - 32)) & 1; }
int inputLevel(uint8_t g) { return g < 32 ? (GPIO.in >> g) & 1 : (GPIO.in1.val >> (g - 32)) & 1; }

bool routedToRtc(uint8_t g) {  // pad switched to the RTC/analog domain (ADC, DAC, touch)
  int rtc = rtc_io_num_map[g];
  return rtc >= 0 && rtc_io_desc[rtc].reg && (regRead(rtc_io_desc[rtc].reg) & rtc_io_desc[rtc].mux);
}

PinState sample(const IoPinInfo &p) {
  PinState s{Mode::Input, -1, 0, -1, NAN, nullptr};
  const uint8_t g = p.gpio;
  const uint32_t mux = regRead(GPIO_PIN_MUX_REG[g]);
  s.pull = (mux & FUN_PU) ? 1 : (mux & FUN_PD) ? -1 : 0;

  if (p.type == IoType::Analog) {
    // Never read the ADC here; show the owner's last sample instead.
    s.mode = Mode::Analog;
    if (g == PIN_PRESSURE) {
      const Machine::Status m = Machine::status();
      s.mv = m.pressurePinMv;
      if (m.pressureValid) s.bar = m.pressureBar;
      else s.note = "Sensor out of range (live, unsmoothed)";
    } else if (isAdc2(g) && WiFi.getMode() != WIFI_OFF) {
      s.note = "ADC2 pin: not readable while Wi-Fi is on";
    } else if (!isAdc1(g) && !isAdc2(g)) {
      s.note = "No ADC on this pin";
    } else {
      s.note = "Not sampled (no owner reports a value)";
    }
    return s;
  }

  const uint32_t mcuSel = (mux >> MCU_SEL_S) & MCU_SEL_V;
  const bool out = outputEnabled(g);
  // IO_MUX function 2 is GPIO on every pin; function 0 is also GPIO except on
  // GPIO1/3 (UART0) and GPIO12-15 (JTAG).
  const bool f0IsGpio = !(g == 1 || g == 3 || (g >= 12 && g <= 15));
  const bool gpioFunc = mcuSel == PIN_FUNC_GPIO || (mcuSel == 0 && f0IsGpio);
  if (routedToRtc(g)) {
    s.mode = Mode::Analog;
  } else if (!gpioFunc || (out && GPIO.func_out_sel_cfg[g].func_sel != SIG_GPIO_OUT_IDX)) {
    s.mode = Mode::Peripheral;  // IO_MUX function (e.g. UART0) or GPIO-matrix peripheral
    if (mcuSel == 0 && g >= 12 && g <= 15) s.note = "JTAG (reset default)";
  } else {
    s.mode = out ? Mode::Output : Mode::Input;
  }

  if (s.mode == Mode::Output) {
    s.level = outputLevel(g);
  } else if (s.mode != Mode::Analog) {
    // Read-only: the level is only meaningful if the pad's input buffer is
    // already on (set by whoever configured the pin). We never switch it on
    // ourselves, so this page can't change any pin's electrical state.
    if (mux & FUN_IE) {
      s.level = inputLevel(g);
    } else if (!s.note) {
      s.note = "Input buffer off (pin not configured)";
    }
  }
  return s;
}

}  // namespace

namespace IoStatus {

void begin() {
  lastSample = millis() - 1000;
}

void loop() {
  if (millis() - lastSample < 250) return;
  lastSample = millis();
  for (size_t i = 0; i < kPinCount; ++i) {
    PinState s = sample(kPins[i]);
    portENTER_CRITICAL(&lock);
    states[i] = s;
    portEXIT_CRITICAL(&lock);
  }
}

String json() {
  JsonDocument doc;
  JsonArray arr = doc["pins"].to<JsonArray>();
  for (size_t i = 0; i < kPinCount; ++i) {
    portENTER_CRITICAL(&lock);
    PinState s = states[i];
    portEXIT_CRITICAL(&lock);
    const IoPinInfo &p = kPins[i];
    JsonObject o = arr.add<JsonObject>();
    o["gpio"] = p.gpio;
    o["macro"] = p.macro;
    o["name"] = p.name;
    o["type"] = typeName(p.type);
    o["notes"] = p.notes;
    o["mode"] = modeName(s.mode);
    if (s.level >= 0) o["level"] = s.level;
    o["pull"] = s.pull > 0 ? "up" : s.pull < 0 ? "down" : "";
    if (s.mv >= 0) o["mv"] = s.mv;
    if (!isnan(s.bar)) o["bar"] = serialized(String(s.bar, 2));
    if (s.note) o["note"] = s.note;
  }
  doc["uptime"] = millis() / 1000;
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace IoStatus
