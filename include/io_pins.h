#pragma once
// ===========================================================================
// Gaggia Guru - ESP32-WROOM-32U hardware IO pin map (38-pin ESP32U board)
// ===========================================================================
//
// EVERY ACTIVE PIN LINE MUST FOLLOW THIS FORMAT (parsed at build time by
// scripts/gen_io_pins.py to build the dashboard's "IO Status" page):
//
//   #define <MACRO>  <gpio>   // <Name> | <Type> | <optional notes>
//
//   Name   Label shown on the IO Status page (no '|' characters).
//   Type   One of: Analog, Digital, Communications, Unused
//   Notes  Free text: board label, restrictions, what's connected, ...
//
// Lines that start with "//" are ignored (pins not on a typical 38-pin board,
// or not usable). Each GPIO number may be active only once.
//
// Machine: Gaggia Classic Pro, 120 V / 60 Hz. Stock thermostats removed.
// Heater chain: SSR -> 165 C manual-reset high-limit thermostat -> thermal fuse -> element.
//
// Hardware rules for this build
//   - Every SSR / dimmer drive pin has an external 10k pull-down so loads stay
//     OFF while the ESP32 is in reset, booting or being flashed.
//   - Coffee/Steam switches feed 3.3 V (never 5 V). Internal pull-down is
//     enabled in firmware; add external 10k pull-down + 100 nF for noise.
//   - RobotDyn dimmer VCC = 3.3 V (its Z-C output is pulled up to VCC).
//   - MAX31855K board VCC = 3.3 V (chip max 3.6 V). Conversion ~100 ms (<= 10 Hz).
//     Thermocouple MUST be ungrounded/insulated junction: a probe touching the
//     earthed boiler makes the MAX31855 report a short-to-GND fault.
//   - XDB401 pressure sensor: 5 V supply, 0.5-4.5 V out through a 10k/10k
//     divider into the ADS1115 (I2C), not an ESP32 pin. GPIO36 is not connected.
//
// Quick reference
//   Input only, no internal pull-ups/downs : GPIO34, 35, 36 (VP), 39 (VN)
//   Strapping pins (level at reset matters): GPIO0, 2, 5, 12, 15
//   ADC1 (usable with Wi-Fi on)            : GPIO32-39
//   ADC2 (NOT usable while Wi-Fi is on)    : GPIO0, 2, 4, 12-15, 25-27
//   GPIO36/39 can raise false interrupts while the ADC is active: no ISRs there.
// ===========================================================================

// ---- Left header (top to bottom, antenna end up): 3V3, EN, then ------------
#define PIN_GPIO36        36  // GPIO36 | Unused | Spare (J12). Board VP. Input only, no pull-ups. ADC1_0. No interrupts (errata)
#define PIN_GPIO39        39  // GPIO39 | Unused | Spare (J12). Board VN. Input only, no pull-ups. ADC1_3. No interrupts (errata)
#define PIN_PUMP_ZC       34  // PUMP_ZC | Digital | RobotDyn dimmer Z-C input (interrupt). 120 zero-crosses/s at 60 Hz. Input only
#define PIN_HX711_DOUT    35  // HX711_DOUT | Communications | Scale load-cell data (future, not fitted). Input only
#define PIN_GPIO32        32  // GPIO32 | Unused | Spare (J12; flow meter / tank level / display DC). ADC1_4, touch T9
#define PIN_GPIO33        33  // GPIO33 | Unused | Spare (J12; flow meter / tank level / display RST). ADC1_5, touch T8
#define PIN_PUMP_PSM      25  // PUMP_PSM | Digital | RobotDyn dimmer PSM/gate output. Pulse-skip control. 10k pull-down
#define PIN_HEATER_SSR    26  // HEATER_SSR | Digital | Boiler heater SSR (via thermal fuses). 10k pull-down
#define PIN_VALVE_SSR     27  // VALVE_SSR | Digital | 3-way solenoid valve SSR. 10k pull-down
#define PIN_STEAM_LIGHT   14  // STEAM_LIGHT | Digital | Steam temperature light SSR. Pulses at boot (harmless flicker)
#define PIN_GPIO12        12  // GPIO12 | Unused | Spare, avoid. Strapping (MTDI): must be LOW at reset. ADC2_5
#define PIN_COFFEE_LIGHT  13  // COFFEE_LIGHT | Digital | Coffee temperature light SSR
// #define PIN_GPIO9     9  // FLASH_D2 | Communications | Board SD2. Internal SPI flash - do not use
// #define PIN_GPIO10   10  // FLASH_D3 | Communications | Board SD3. Internal SPI flash - do not use
// #define PIN_GPIO11   11  // FLASH_CMD | Communications | Board CMD. Internal SPI flash - do not use
//                          (then 5V)

// ---- Right header (top to bottom): GND, then -------------------------------
#define PIN_SPI_MOSI      23  // SPI_MOSI | Unused | Spare SPI port J13 MOSI (VSPI, via 33R). Not used by firmware yet
#define PIN_I2C_SCL       22  // I2C_SCL | Communications | I2C bus: ADS1115 pressure ADC, OLED, tank sensor
#define PIN_UART0_TX       1  // UART0_TX | Communications | Board TX0. USB serial / programming
#define PIN_UART0_RX       3  // UART0_RX | Communications | Board RX0. USB serial / programming
#define PIN_I2C_SDA       21  // I2C_SDA | Communications | I2C bus: ADS1115 pressure ADC, OLED, tank sensor
#define PIN_TC_SO         19  // TC_SO | Communications | MAX31855 SO, boiler thermocouple (VSPI MISO). Also SPI port J13 MISO
#define PIN_TC_SCK        18  // TC_SCK | Communications | MAX31855 SCK, boiler thermocouple (VSPI CLK). Also SPI port J13 SCK (via 33R)
#define PIN_TC_CS          5  // TC_CS | Communications | MAX31855 chip select, boiler thermocouple. Strapping: HIGH at reset (CS idles high)
#define PIN_STEAM_SW      17  // STEAM_SW | Digital | Steam switch, 3.3 V = on. Internal pull-down
#define PIN_COFFEE_SW     16  // COFFEE_SW | Digital | Coffee switch, 3.3 V = on. Internal pull-down
#define PIN_HX711_SCK      4  // HX711_SCK | Communications | Scale load-cell clock (future, not fitted)
#define PIN_GPIO0          0  // GPIO0 | Unused | Spare, avoid. BOOT button. Strapping: LOW at reset = download mode
#define PIN_STATUS_LED     2  // STATUS_LED | Digital | On-board LED. Strapping: LOW/float at reset
#define PIN_SPI_CS        15  // SPI_CS | Unused | Spare SPI port J13 CS, 10k pull-up. Strapping (MTDO): HIGH at reset. Not used by firmware yet
// #define PIN_GPIO8     8  // FLASH_D1 | Communications | Board SD1. Internal SPI flash - do not use
// #define PIN_GPIO7     7  // FLASH_D0 | Communications | Board SD0. Internal SPI flash - do not use
// #define PIN_GPIO6     6  // FLASH_CLK | Communications | Board CLK. Internal SPI flash - do not use

// ---- ESP32 chip pins not brought out on the WROOM-32U module ---------------
// #define PIN_GPIO37   37  // GPIO37 | Unused | Not on WROOM module (SENSOR_CAPP)
// #define PIN_GPIO38   38  // GPIO38 | Unused | Not on WROOM module (SENSOR_CAPN)
