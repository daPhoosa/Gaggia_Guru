#include "io_status.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_struct.h>
#include <soc/io_mux_reg.h>
#include <soc/rtc_io_periph.h>

#include "io_pins.h"
#include "machine_config.h"
#include "sensors.h"

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
  PinState s{Mode::Input, -1, 0, -1, nullptr};
  const uint8_t g = p.gpio;
  const uint32_t mux = regRead(GPIO_PIN_MUX_REG[g]);
  s.pull = (mux & FUN_PU) ? 1 : (mux & FUN_PD) ? -1 : 0;

  if (p.type == IoType::Analog) {
    // Never read the ADC here; show the owner's last sample instead.
    s.mode = Mode::Analog;
    if (isAdc2(g) && WiFi.getMode() != WIFI_OFF) {
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

// ADS1115 inputs (PCB U3, I2C 0x48). Used inputs also get engineering units.
struct AdcInfo {
  const char *name;
  bool used;
  const char *notes;
};
const AdcInfo kAdc[Sensors::kAdsChannels] = {
    {"Brew pressure", true,
     "XDB401 output (0.5-4.5 V = 0-12 bar) through a 10k/10k divider. Bar here is computed from this one "
     "sample with the 5 V correction; the controller and dashboard use a smoothed value."},
    {"5 V rail", true,
     "5 V supply through an identical 10k/10k divider. Corrects the ratiometric pressure reading "
     "(read about once a second)."},
    {"Spare (J14 pin 4)", false, "Direct to U3, 0-3.3 V only. Not used by the firmware; floats when nothing is connected."},
    {"Spare (J14 pin 5)", false, "Direct to U3, 0-3.3 V only. Not used by the firmware; floats when nothing is connected."},
};

float round_to(float v, float step) { return roundf(v / step) * step; }

void adcJson(JsonArray arr) {
  Sensors::AdsSample s[Sensors::kAdsChannels];
  Sensors::adsSamples(s);
  for (uint8_t ch = 0; ch < Sensors::kAdsChannels; ++ch) {
    const AdcInfo &a = kAdc[ch];
    JsonObject o = arr.add<JsonObject>();
    o["ch"] = ch;
    o["name"] = a.name;
    o["used"] = a.used;
    o["notes"] = a.notes;
    if (!s[ch].valid) {
      o["note"] = s[ch].atMs == 0 ? "Not read yet" : "ADS1115 not responding";
      continue;
    }
    o["raw"] = s[ch].raw;
    o["mv"] = round_to(s[ch].mv, 0.1f);
    if (ch == 0) {
      const Sensors::PressureReading pr = Sensors::pressureFromPinMv(s[0].mv, s[1].valid ? s[1].mv : 0);
      o["value"] = round_to(pr.bar, 0.01f);
      o["unit"] = "bar";
      if (!pr.ok) o["note"] = "Outside the sensor's valid range (disconnected?)";
    } else if (ch == 1) {
      o["value"] = round_to(s[1].mv / 1000.0f / PRESSURE_DIVIDER_RATIO, 0.001f);
      o["unit"] = "V";
    }
  }
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
    if (s.note) o["note"] = s.note;
  }
  adcJson(doc["adc"].to<JsonArray>());
  doc["uptime"] = millis() / 1000;
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace IoStatus
