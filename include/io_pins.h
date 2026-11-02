#pragma once
// ===========================================================================
// ESP32-WROOM-32U hardware IO pin map (38-pin prototype/DevKit board)
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
// When you give a pin a job, update its line, e.g.
//   #define PIN_GPIO4   4   // GPIO4 | Unused | ADC2_0, touch T0
// becomes
//   #define PIN_RELAY_1 4   // RELAY_1 | Digital | drives K1 via Q1
//
// Lines that start with "//" are ignored (pins not on a typical 38-pin board,
// or not usable). Each GPIO number may be active only once.
//
// Quick reference
//   Input only, no internal pull-ups/downs : GPIO34, 35, 36 (VP), 39 (VN)
//   Strapping pins (level at reset matters): GPIO0, 2, 5, 12, 15
//   ADC1 (usable with Wi-Fi on)            : GPIO32-39
//   ADC2 (NOT usable while Wi-Fi is on)    : GPIO0, 2, 4, 12-15, 25-27
//   DAC                                    : GPIO25 (DAC1), GPIO26 (DAC2)
// ===========================================================================

// ---- Left header (top to bottom, antenna end up): 3V3, EN, then ------------
#define PIN_GPIO36      36  // GPIO36 | Unused | Board VP. Input only, no pull-ups. ADC1_0
#define PIN_GPIO39      39  // GPIO39 | Unused | Board VN. Input only, no pull-ups. ADC1_3
#define PIN_GPIO34      34  // GPIO34 | Unused | Input only, no pull-ups. ADC1_6
#define PIN_GPIO35      35  // GPIO35 | Unused | Input only, no pull-ups. ADC1_7
#define PIN_GPIO32      32  // GPIO32 | Unused | ADC1_4, touch T9, XTAL32_P
#define PIN_GPIO33      33  // GPIO33 | Unused | ADC1_5, touch T8, XTAL32_N
#define PIN_GPIO25      25  // GPIO25 | Unused | DAC1, ADC2_8
#define PIN_GPIO26      26  // GPIO26 | Unused | DAC2, ADC2_9
#define PIN_GPIO27      27  // GPIO27 | Unused | ADC2_7, touch T7
#define PIN_GPIO14      14  // GPIO14 | Unused | ADC2_6, touch T6, HSPI CLK. Outputs PWM at boot
#define PIN_GPIO12      12  // GPIO12 | Unused | Strapping (MTDI): must be LOW at reset. ADC2_5, HSPI MISO
#define PIN_GPIO13      13  // GPIO13 | Unused | ADC2_4, touch T4, HSPI MOSI
// #define PIN_GPIO9     9  // FLASH_D2 | Communications | Board SD2. Internal SPI flash - do not use
// #define PIN_GPIO10   10  // FLASH_D3 | Communications | Board SD3. Internal SPI flash - do not use
// #define PIN_GPIO11   11  // FLASH_CMD | Communications | Board CMD. Internal SPI flash - do not use
//                          (then 5V)

// ---- Right header (top to bottom): GND, then -------------------------------
#define PIN_GPIO23      23  // GPIO23 | Unused | VSPI MOSI
#define PIN_GPIO22      22  // GPIO22 | Unused | Default I2C SCL
#define PIN_UART0_TX     1  // UART0_TX | Communications | Board TX0. USB serial / programming
#define PIN_UART0_RX     3  // UART0_RX | Communications | Board RX0. USB serial / programming
#define PIN_GPIO21      21  // GPIO21 | Unused | Default I2C SDA
#define PIN_GPIO19      19  // GPIO19 | Unused | VSPI MISO
#define PIN_GPIO18      18  // GPIO18 | Unused | VSPI CLK
#define PIN_GPIO5        5  // GPIO5 | Unused | Strapping: HIGH at reset. VSPI CS. Outputs PWM at boot
#define PIN_GPIO17      17  // GPIO17 | Unused | Default UART2 TX
#define PIN_GPIO16      16  // GPIO16 | Unused | Default UART2 RX
#define PIN_GPIO4        4  // GPIO4 | Unused | ADC2_0, touch T0
#define PIN_GPIO0        0  // GPIO0 | Unused | BOOT button. Strapping: LOW at reset = download mode
#define PIN_STATUS_LED   2  // STATUS_LED | Digital | On-board LED. Strapping: LOW/float at reset
#define PIN_GPIO15      15  // GPIO15 | Unused | Strapping (MTDO): HIGH at reset. ADC2_3, HSPI CS
// #define PIN_GPIO8     8  // FLASH_D1 | Communications | Board SD1. Internal SPI flash - do not use
// #define PIN_GPIO7     7  // FLASH_D0 | Communications | Board SD0. Internal SPI flash - do not use
// #define PIN_GPIO6     6  // FLASH_CLK | Communications | Board CLK. Internal SPI flash - do not use

// ---- ESP32 chip pins not brought out on the WROOM-32U module ---------------
// #define PIN_GPIO37   37  // GPIO37 | Unused | Not on WROOM module (SENSOR_CAPP)
// #define PIN_GPIO38   38  // GPIO38 | Unused | Not on WROOM module (SENSOR_CAPN)
