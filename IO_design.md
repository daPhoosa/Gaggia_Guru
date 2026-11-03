# Gaggia Savant — IO Design

Hardware reference for the ESP32-WROOM-32U controller in a converted Gaggia Classic Pro
(120 V / 60 Hz). It covers every pin assignment and the signal conditioning each
connection expects.

The pin numbers here must match [`include/io_pins.h`](include/io_pins.h), which is what the
firmware uses. If you move a pin, update both. The build checks `io_pins.h` for duplicates
and illegal pins, but it cannot check this document.

The carrier board that implements this is the KiCad 10 project in [`PCB/`](PCB/)
(`GaggiaSavant.kicad_pro`). Its schematic follows this document; if they disagree, fix one.

---

## 1. Pin assignments

| Signal | GPIO | Dir | Macro | Connects to | Firmware pin mode |
|---|---|---|---|---|---|
| Boiler heater SSR | 26 | Out | `PIN_HEATER_SSR` | Heater SSR input (via driver, §3.1) | `OUTPUT`, LOW at boot |
| Solenoid valve SSR | 27 | Out | `PIN_VALVE_SSR` | Valve SSR input (via driver, §3.1) | `OUTPUT`, LOW at boot |
| Pump dimmer PSM | 25 | Out | `PIN_PUMP_PSM` | RobotDyn dimmer `PSM` | `OUTPUT`, LOW at boot |
| Pump dimmer Z-C | 34 | In | `PIN_PUMP_ZC` | RobotDyn dimmer `Z-C` | `INPUT`, RISING interrupt |
| Coffee light SSR | 13 | Out | `PIN_COFFEE_LIGHT` | Coffee lamp SSR input | `OUTPUT`, LOW at boot |
| Steam light SSR | 14 | Out | `PIN_STEAM_LIGHT` | Steam lamp SSR input | `OUTPUT`, LOW at boot |
| Coffee switch | 16 | In | `PIN_COFFEE_SW` | Coffee switch (3.3 V = on) | `INPUT_PULLDOWN` |
| Steam switch | 17 | In | `PIN_STEAM_SW` | Steam switch (3.3 V = on) | `INPUT_PULLDOWN` |
| Thermocouple SCK | 18 | Out | `PIN_TC_SCK` | MAX31855K `SCK` | `OUTPUT` (bit-banged) |
| Thermocouple SO | 19 | In | `PIN_TC_SO` | MAX31855K `SO` / `DO` | `INPUT` |
| Thermocouple CS | 5 | Out | `PIN_TC_CS` | MAX31855K `CS` | `OUTPUT`, idles HIGH |
| Brew pressure | 36 (VP) | Analog in | `PIN_PRESSURE` | XDB401 output via divider (§3.6) | ADC1_CH0, 11 dB attenuation |
| Status LED | 2 | Out | `PIN_STATUS_LED` | On-board LED | `OUTPUT` |
| USB serial TX / RX | 1 / 3 | — | `PIN_UART0_TX/RX` | USB-UART bridge | UART0 |
| *Reserved:* HX711 DOUT | 35 | In | `PIN_HX711_DOUT` | HX711 `DOUT` (future scale) | not used yet |
| *Reserved:* HX711 SCK | 4 | Out | `PIN_HX711_SCK` | HX711 `PD_SCK` (future scale) | not used yet |
| I²C SDA | 21 | I/O | `PIN_I2C_SDA` | ADS1115 (PCB U3), future I²C devices | I²C, 400 kHz, when `PRESSURE_ADC_ADS1115` |
| I²C SCL | 22 | Out | `PIN_I2C_SCL` | ADS1115 (PCB U3), future I²C devices | I²C, 400 kHz, when `PRESSURE_ADC_ADS1115` |

### Spare pins

| GPIO | Notes | Suitable for |
|---|---|---|
| 32, 33 | ADC1, interrupt-capable, internal pull-up/down | Best spares: flow meter, tank level, extra analog input |
| 23 | Plain GPIO (VSPI MOSI) | Any digital in/out |
| 39 (VN) | Input only, no pulls, false-interrupt errata | Analog input only; no interrupts |
| 0, 12, 15 | Strapping pins (§4) | Avoid; last resort, and never for loads |

---

## 2. Power

| Rail | Source | Feeds | Notes |
|---|---|---|---|
| 5 V | Isolated mains-to-5 V supply, ≥ 1 A | ESP32 board (5V/VIN), XDB401 sensor, SSR drivers | Must be an isolated module (e.g. HLK-PM05 class) |
| 3.3 V | Regulator, ≥ 600 mA (the DevKit's on-board regulator is fine) | ESP32, MAX31855K, RobotDyn dimmer VCC, switch supply, HX711 / I²C (future) | Wi-Fi draws ~500 mA spikes |
| GND | Common | Everything low-voltage | Join the 5 V, 3.3 V and sensor grounds at one point near the ESP32 |

- **Only SSRs and the dimmer cross the mains boundary.** Each provides isolation, so no
  low-voltage wire may connect to a mains terminal.
- Keep low-voltage wiring (especially the thermocouple and pressure leads) physically
  separated from mains wiring, and away from the pump and heater leads.
- Fit a 100 nF ceramic decoupling capacitor at the supply pin of each module that doesn't
  already have one.

**Approximate load on the 5 V rail:**

| Load | Current |
|---|---|
| ESP32 with Wi-Fi | ~250 mA average, 500 mA peaks |
| 4 SSR inputs | ~10–15 mA each |
| 5 indicator LEDs (power + 4 SSR) | ~3 mA each |
| XDB401 | < 5 mA |
| Dimmer logic, MAX31855K | < 15 mA |

A 1 A supply leaves a comfortable margin.

**3.3 V rail: no separate regulator needed.** Everything on 3.3 V that isn't the ESP32 is
drawn from the DevKit's 3V3 pin (its AMS1117-class regulator, about 1 A max):

| 3.3 V load (non-DevKit) | Current |
|---|---|
| MAX31855K | ~1.5 mA |
| ADS1115 | ~0.2 mA |
| Dimmer Z-C pull-up | < 1 mA |
| Switch LEDs (2 × 330 Ω) and pull-downs | ~9 mA with both switches on |
| I²C pull-ups (when low) | ~1.4 mA |
| HX711, small OLED (future) | ~1.5 mA, ~20 mA |
| **Total** | **< 40 mA** |

With the ESP32's own ~250 mA average (500 mA peaks) that is well inside the regulator's
rating. Its dissipation is about (4.7 V − 3.3 V) × 0.3 A ≈ 0.4 W: warm, not hot. Revisit
only if a 3.3 V load of more than ~150 mA is added; give that its own regulator from 5 V.
The analog parts tolerate Wi-Fi ripple on 3V3: the ADS1115 and MAX31855K use internal
references, and each has its own 100 nF.

---

## 3. Signal conditioning per connection

### 3.1 SSR outputs — heater, valve, coffee light, steam light (GPIO 26, 27, 13, 14)

- **Pull-down:** a 10 kΩ resistor from each GPIO to GND (mandatory). It keeps the load
  OFF while the ESP32 is in reset, booting or being flashed, before firmware drives the
  pin.
- **Driver:** a 3.3 V GPIO sits at the bottom of the input range of typical "3–32 V DC"
  SSRs, so each SSR is switched from 5 V by a low-side logic-level N-MOSFET (AO3400A,
  SOT-23, fully on at 2.5 V gate drive):

  ```
  GPIO ──┬── 1 kΩ ── G   AO3400A        5V ──┬────────── SSR(+)
         │                                   1 kΩ
       10 kΩ                                 LED (red)
         │                                   │
        GND                     D ───────────┴────────── SSR(−)
                                S ── GND
  ```

  The 1 kΩ gate resistor is the same value as the switch filters (fewer unique parts);
  switching speed is irrelevant for SSRs. Logic is unchanged: GPIO HIGH turns the load on.
- **Indicator LED:** 1 kΩ + red LED (about 3 mA) across the SSR input, so it lights exactly
  when the SSR input is energised.
- **Direct drive** is acceptable only if the SSR datasheet guarantees turn-on at ≤ 3.0 V
  and draws ≤ 12 mA. Measure the input current first.
- **Heater SSR:** a zero-cross DC-input SSR rated **40 A** (the heater draws about 9–12 A
  at 120 V), on a heatsink. The firmware switches it in time-proportioned 1 s windows.
- **Valve and lamp SSRs:** zero-cross DC-input SSRs. A small rating is fine, but check the
  SSR's minimum load current: lamps and the solenoid draw little, and an SSR's off-state
  leakage can keep a small load glowing or humming. Add a bleeder resistor or snubber across
  the load if so.
- **GPIO 14 (steam light)** puts out PWM pulses for a moment at boot. The pull-down and the
  SSR's switching at mains zero-cross mean this shows as a brief flicker at worst, which is
  harmless on a lamp.

### 3.2 Pump dimmer — RobotDyn AC dimmer (PSM = GPIO 25, Z-C = GPIO 34)

- **Dimmer VCC = 3.3 V.** The Z-C output is pulled up to VCC on the module, so a 5 V VCC
  would put 5 V on GPIO 34.
- **PSM (GPIO 25):** fit a 10 kΩ pull-down at the GPIO so the triac stays off during boot.
  Firmware holds the gate for a whole mains cycle (pulse-skip modulation, 60 steps/s at
  60 Hz). It never uses phase-angle firing.
- **Z-C (GPIO 34):** input-only pin, so there is no internal pull and the module's pull-up
  is relied on. The firmware takes rising edges and ignores any edge within 4 ms of the
  previous one. If you see noise, add 100 pF–1 nF from Z-C to GND at the ESP32 end.
- **Expected signal:** 120 pulses/s at 60 Hz. The dashboard's **Mains** reading should show
  about 60 Hz. If the signal is lost while the pump runs, the firmware latches a fault.
- **Triac rating:** the pump is an Ulka vibratory pump of about 50 W, well inside the
  module's rating. Its internal diode means only one half-cycle drives it, which is why the
  firmware switches in full cycles.

### 3.3 Coffee and Steam switches (GPIO 16, 17)

- The switches are fully rewired to low voltage: 3.3 V when on, open when off. **3.3 V
  only — never 5 V**, and no mains on any switch terminal.
- Use the ESP32's own 3.3 V rail as the switch supply.
- **Conditioning at the ESP32 end:**

  ```
  3V3 ── switch ──┬── 1 kΩ ──┬── GPIO
                  │          │
                10 kΩ      100 nF
                  │          │
                 GND        GND
  ```

  - The **10 kΩ external pull-down** sets a firm off level. The internal pull-down is also
    enabled, but it is weak (~45 kΩ) for wires running next to mains.
  - The **1 kΩ series resistor + 100 nF capacitor** filter induced noise and protect the
    pin from ESD.
  - **Indicator LED:** 330 Ω + red LED (about 4 mA) from the switch side of the 1 kΩ to
    GND, lit while the switch is on. Its current comes from the 3.3 V rail through the switch.
- **Debounce:** the firmware needs 2 consecutive equal samples 50 ms apart (about 100 ms).
- **Behaviour:** if Coffee is on at power-up, the pump won't start until the switch is
  turned off and on again.

### 3.4 Thermocouple — MAX31855K (SCK = 18, SO = 19, CS = 5)

- **Chip:** **MAX31855K** (the K-type variant).
  - Supply: **3.3 V**; the chip's absolute maximum is 3.6 V. Breakout boards with their
    own regulator may be fed 5 V, but their logic must still be 3.3 V.
- **Thermocouple:** K-type, **ungrounded (insulated) junction**. This is confirmed for this
  build. A grounded probe on the earthed boiler makes the chip report a short-to-GND fault on
  every read.
- **Filtering:** fit 10 nF across T+/T− at the chip (most breakouts include it). Use
  thermocouple extension wire, twisted, kept away from the heater and mains leads.
- **Mounting:** firm mechanical contact with thermal paste, and strain-relieve the lead. A
  loose probe reads low, which drives the heater flat out; the firmware's heating check and
  the high-limit thermostat are the backstops.
- **Bus:** the firmware bit-bangs the bus, reading 32 bits about 1 µs per clock. It reads
  every 200 ms; the chip converts in about 100 ms.
- **CS on GPIO 5:** a strapping pin that must be HIGH at reset, which matches CS's idle
  level. The brief pulses on it at boot are harmless to the MAX31855.
- **Fault handling:** 5 consecutive bad reads latch a fault. A bad read is open circuit,
  short to GND or VCC, no response (all 0s or all 1s), or a reading outside 0–250 °C.

### 3.5 Boiler heater circuit (mains side, for reference)

```
L ── SSR (GPIO 26) ── 165 °C manual-reset high-limit thermostat ── thermal fuse ── element ── N
```

- The stock thermostats are removed. The **165 °C manual-reset** high-limit thermostat and
  the thermal fuse are the hardware backstops if the SSR fails shorted.
- Mount the high-limit on the boiler with thermal paste, and make sure its reset button can
  be reached (unplug the machine first).
- Firmware safety limits sit below the hardware ones:
  - The highest steam setpoint is 150 °C.
  - The over-temperature fault trips at 155 °C.
  - If the heater runs at ≥ 90 % for 120 s with less than 2 °C of rise and the pump off,
    the firmware latches a "not heating" fault.

### 3.6 Brew pressure — XDB401 (GPIO 36)

- **Sensor:** XDB401, 0–1.2 MPa (0–12 bar gauge), **5 V supply**, output 0.5–4.5 V.
- **Divider (mandatory):** the sensor's 4.5 V would exceed the ESP32's 3.3 V limit.

  ```
  XDB401 OUT ── 10 kΩ ──┬── GPIO 36 and ADS1115 AIN0
                        │
                      10 kΩ   100 nF
                        │       │
                       GND     GND
  ```

  | Sensor output | Pressure | Pin voltage |
  |---|---|---|
  | 0.5 V | 0 bar | 0.25 V |
  | 2.5 V | 6 bar | 1.25 V |
  | 4.5 V | 12 bar | 2.25 V |

  - Use 1 % resistors (every 10 kΩ on the board is 1 %, so this is one part). The firmware
    assumes a ratio of exactly 0.5 (`PRESSURE_DIVIDER_RATIO`); tolerance shows up directly
    as pressure error.
  - The **100 nF** capacitor at the pin gives the converters a low-impedance source and
    filters pump pulsation.
  - The 2.25 V maximum (2.375 V at the 4.75 V validity limit) keeps readings inside the ESP32
    ADC's linear range at 11 dB attenuation (about 0.15–2.45 V).
- **Converter: build option `PRESSURE_ADC`** (set in `platformio.ini` `build_flags`). Both
  converters see the same node, so the choice needs no hardware change:

  | Option | Converter | Notes |
  |---|---|---|
  | `PRESSURE_ADC_NATIVE` (default) | ESP32 ADC1, GPIO 36 | 8-sample average per tick; ESP32 ADC accuracy (a few %) |
  | `PRESSURE_ADC_ADS1115` | ADS1115 (PCB U3) on I²C, address 0x48 | 16-bit, ±4.096 V range, 475 SPS single-shot. AIN1 measures the 5 V rail through an identical 10k/10k divider about once a second; the reading is scaled by 5 V ÷ measured supply, because the XDB401's output is ratiometric. No correction if the measured supply is outside 4.5–5.5 V. |

  With the ADS1115 option, an I²C failure makes the reading invalid: shown as such on the
  dashboard, and a fault ("Pressure sensor: ADS1115 not responding") if it lasts 500 ms
  while brewing. The machine JSON then also reports `supplyV`.
- **Optional protection:** a BAT54S Schottky clamp from the pin to 3.3 V and GND. This is
  only needed if the sensor could ever be powered above 5 V: at 12 V the divider alone would
  pass about 6 V.
- **Valid range:** the firmware treats a sensor output below 0.35 V (disconnected or
  unpowered) or above 4.75 V as invalid. Invalid for 500 ms while brewing latches a fault;
  in other states the reading is shown but not used.
- **Sampling:** only the control task reads the converter. Every 50 ms it takes a reading
  (native: 8 averaged samples; ADS1115: one conversion), smooths the pin voltage (EMA,
  α = 0.3) and converts it to bar. The pressure PID, the
  dashboard and the IO Status page (pin voltage + bar) all use that one smoothed value.
  While the sensor is out of range, the IO Status page shows the live, unsmoothed voltage
  with a warning instead.

### 3.7 Future: scale — HX711 (DOUT = 35, SCK = 4)

- Power the HX711 at **3.3 V** so DOUT is at 3.3 V logic.
- GPIO 35 is input-only, which is fine because the HX711 drives DOUT itself.
- Holding SCK high for more than 60 µs powers the HX711 down. GPIO 4 floats at boot, so add
  a 10 kΩ pull-down on it.
- Keep the load-cell wiring short and shielded, away from the pump.

### 3.8 I²C bus (SDA = 21, SCL = 22)

- **4.7 kΩ pull-ups to 3.3 V** on SDA and SCL (only once, even with several devices).
- On the PCB: the ADS1115 pressure ADC (U3, address 0x48, used with `PRESSURE_ADC_ADS1115`),
  plus a 4-pin header (GND, 3V3, SCL, SDA) for a status OLED or tank-level sensor. Every
  device must run at 3.3 V logic and have a different address.

---

## 4. Boot and strapping behaviour

| GPIO | Level required at reset | Use in this design | Why it's safe |
|---|---|---|---|
| 0 | HIGH (LOW = download mode) | Spare (BOOT button) | Unused |
| 2 | LOW or floating | Status LED | LED doesn't pull it high |
| 5 | HIGH | Thermocouple CS | CS idles high; the boot pulses are harmless |
| 12 | LOW (sets flash voltage) | Spare | Unused; never pull it high |
| 15 | HIGH | Spare | Unused |
| 14 | — (pulses at boot) | Steam light | Only a lamp flicker |

The heater, valve and pump sit on pins (26, 27, 25) with no strapping role and no boot
pulses. The lamps use 13 and 14, where a boot pulse only flickers a light. Every load pin
has a 10 kΩ pull-down. The firmware also drives
them LOW as the very first thing in `setup()`, before Serial or Wi-Fi start.

---

## 5. Bring-up checklist

Do this with the **heater disconnected** or the boiler empty until step 6.

1. Power the low-voltage side only. Check 5 V and 3.3 V, and confirm no GPIO sits above
   3.3 V: in particular Z-C (34), the switches (16, 17) and the pressure pin (36, which
   should read 0.27 V at 0 bar).
2. Flash the firmware and the web files. Open **IO Status** and confirm each pin's
   name and mode.
3. Flip Coffee and Steam. The dashboard state should go IDLE → BREWING / STEAM /
   HOT_WATER. At power-up with Coffee on, the dashboard should report that the switch needs
   cycling.
4. Thermocouple: room temperature should read sensibly. Unplug the probe and confirm the
   "open circuit" fault, then power-cycle.
5. With mains on, the dashboard **Mains** reading should show about 60 Hz. Test the pump
   and valve briefly with water in the tank.
6. Reconnect the heater. Watch the first warm-up to 107 °C and tune the PID gains on the
   **Settings** page if it overshoots.
7. Check the pressure reading against a portafilter pressure gauge.
