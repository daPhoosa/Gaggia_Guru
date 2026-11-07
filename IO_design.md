# Gaggia Guru — IO Design

Hardware reference for the ESP32-WROOM-32U controller in a converted Gaggia Classic Pro
(120 V / 60 Hz). It covers every pin assignment and the signal conditioning each
connection expects.

The pin numbers here must match [`include/io_pins.h`](include/io_pins.h), which is what the
firmware uses. If you move a pin, update both. The build checks `io_pins.h` for duplicates
and illegal pins, but it cannot check this document.

The carrier board that implements this is the KiCad 10 project in [`PCB/`](PCB/)
(`GaggiaGuru.kicad_pro`). Its schematic follows this document; if they disagree, fix one.

---

## 1. Pin assignments

| Signal | GPIO | Dir | Macro | Connects to | Firmware pin mode |
|---|---|---|---|---|---|
| Boiler heater SSR | 26 | Out | `PIN_HEATER_SSR` | Heater SSR input (via driver, §3.1) | `OUTPUT`, LOW at boot |
| Solenoid valve SSR | 27 | Out | `PIN_VALVE_SSR` | Valve SSR input (via driver, §3.1) | `OUTPUT`, LOW at boot |
| Pump dimmer PSM | 25 | Out | `PIN_PUMP_PSM` | RobotDyn dimmer `PSM` | `OUTPUT`, LOW at boot |
| Pump dimmer Z-C | 34 | In | `PIN_PUMP_ZC` | RobotDyn dimmer `Z-C` | `INPUT`, RISING interrupt |
| Coffee light relay | 13 | Out | `PIN_COFFEE_LIGHT` | Coffee lamp relay coil (via driver, §3.1) | `OUTPUT`, LOW at boot |
| Steam light relay | 14 | Out | `PIN_STEAM_LIGHT` | Steam lamp relay coil (via driver, §3.1) | `OUTPUT`, LOW at boot |
| Coffee switch | 16 | In | `PIN_COFFEE_SW` | Coffee switch (3.3 V = on) | `INPUT_PULLDOWN` |
| Steam switch | 17 | In | `PIN_STEAM_SW` | Steam switch (3.3 V = on) | `INPUT_PULLDOWN` |
| Thermocouple SCK | 18 | Out | `PIN_TC_SCK` | MAX31855K `SCK` | `OUTPUT` (bit-banged) |
| Thermocouple SO | 19 | In | `PIN_TC_SO` | MAX31855K `SO` / `DO` | `INPUT` |
| Thermocouple CS | 5 | Out | `PIN_TC_CS` | MAX31855K `CS` | `OUTPUT`, idles HIGH |
| Status LED | 2 | Out | `PIN_STATUS_LED` | On-board LED | `OUTPUT` |
| USB serial TX / RX | 1 / 3 | — | `PIN_UART0_TX/RX` | USB-UART bridge | UART0 |
| *Reserved:* HX711 DOUT | 35 | In | `PIN_HX711_DOUT` | HX711 `DOUT` (future scale) | not used yet |
| *Reserved:* HX711 SCK | 4 | Out | `PIN_HX711_SCK` | HX711 `PD_SCK` (future scale) | not used yet |
| I²C SDA | 21 | I/O | `PIN_I2C_SDA` | ADS1115 brew-pressure ADC (PCB U3, §3.6), future I²C devices | I²C, 400 kHz |
| I²C SCL | 22 | Out | `PIN_I2C_SCL` | ADS1115 brew-pressure ADC (PCB U3, §3.6), future I²C devices | I²C, 400 kHz |

### Spare pins

| GPIO | Notes | Suitable for |
|---|---|---|
| 32, 33 | ADC1, interrupt-capable, internal pull-up/down (J12) | Best spares: flow meter, tank level, extra analog input, display DC / RST |
| 36 (VP), 39 (VN) | Input only, no pulls, false-interrupt errata (J12) | Analog input only; no interrupts |
| 0, 12 | Strapping pins (§4) | Avoid; last resort, and never for loads |
| 23, 15 | Spare SPI port J13 (MOSI, CS), §3.9 | SPI devices (display, SD card) |

---

## 2. Power

| Rail | Source | Feeds | Notes |
|---|---|---|---|
| 5 V | Isolated mains-to-5 V supply, ≥ 1 A | ESP32 board (5V/VIN), XDB401 sensor, SSR and lamp-relay drivers | Must be an isolated module (e.g. HLK-PM05 class) |
| 3.3 V | Regulator, ≥ 600 mA (the ESP32U's on-board regulator is fine) | ESP32, MAX31855K, RobotDyn dimmer VCC, switch supply, HX711 / I²C (future) | Wi-Fi draws ~500 mA spikes |
| GND | Common | Everything low-voltage | Join the 5 V, 3.3 V and sensor grounds at one point near the ESP32 |

- **Only the SSRs, the lamp relays and the dimmer cross the mains boundary.** Each provides
  isolation, so no
  low-voltage wire may connect to a mains terminal.
- Keep low-voltage wiring (especially the thermocouple and pressure leads) physically
  separated from mains wiring, and away from the pump and heater leads.
- Fit a 100 nF ceramic decoupling capacitor at the supply pin of each module that doesn't
  already have one.

**Approximate load on the 5 V rail:**

| Load | Current |
|---|---|
| ESP32 with Wi-Fi | ~250 mA average, 500 mA peaks |
| 2 SSR inputs (heater, valve) | ~10–15 mA each |
| 2 lamp relay coils (5 V) | ~30 mA each |
| 5 indicator LEDs (power + 4 drivers) | ~3 mA each |
| XDB401 | < 5 mA |
| Dimmer logic, MAX31855K | < 15 mA |

A 1 A supply leaves a comfortable margin.

**3.3 V rail: no separate regulator needed.** Everything on 3.3 V that isn't the ESP32 is
drawn from the ESP32U's 3V3 pin (its AMS1117-class regulator, about 1 A max):

| 3.3 V load (non-ESP32U) | Current |
|---|---|
| MAX31855K | ~1.5 mA |
| ADS1115 | ~0.2 mA |
| Dimmer Z-C pull-up | < 1 mA |
| Switch LEDs (2 × 330 Ω) and pull-downs | ~9 mA with both switches on |
| I²C pull-ups (when low) | ~1.4 mA |
| HX711, small OLED (future) | ~1.5 mA, ~20 mA |
| SPI device on J13 (future) | depends: a TFT's backlight can take 20–100 mA, power it from J13's 5 V pin if its module allows |
| **Total** | **< 40 mA** |

With the ESP32's own ~250 mA average (500 mA peaks) that is well inside the regulator's
rating. Its dissipation is about (4.7 V − 3.3 V) × 0.3 A ≈ 0.4 W: warm, not hot. Revisit
only if a 3.3 V load of more than ~150 mA is added; give that its own regulator from 5 V.
The analog parts tolerate Wi-Fi ripple on 3V3: the ADS1115 and MAX31855K use internal
references, and each has its own 100 nF.

---

## 3. Signal conditioning per connection

Every off-board connector (J1–J14) is a **JST XH** (2.50 mm pitch, vertical, rated 3 A per
pin). Every header that carries both GND and 3.3 V (J10, J11, J12, J13, J14) has
**pin 1 = GND, pin 2 = 3.3 V**. The exception is J6, which follows the RobotDyn dimmer
module's own header order (§3.2).

### 3.1 SSR and relay outputs — heater, valve, coffee light, steam light (GPIO 26, 27, 13, 14)

The heater and valve drive SSRs. The coffee and steam lamps drive external
**electromechanical relays** (5 V coil, about 30 mA), not SSRs: the lamps draw too little
current for an SSR to switch them reliably. All four use the same on-board driver.

- **Pull-down:** a 10 kΩ resistor from each GPIO to GND (mandatory). It keeps the load
  OFF while the ESP32 is in reset, booting or being flashed, before firmware drives the
  pin.
- **Driver:** a 3.3 V GPIO sits at the bottom of the input range of typical "3–32 V DC"
  SSRs (and cannot drive a 5 V relay coil at all), so each SSR input or relay coil is switched
  from 5 V by a low-side logic-level N-MOSFET (AO3400A, SOT-23, fully on at 2.5 V gate drive):

  ```
  GPIO ──┬── 1 kΩ ── G   AO3400A        5V ──┬────────── SSR(+) / coil(+)
         │                                   1 kΩ
       10 kΩ                                 LED (red)
         │                                   │
        GND                     D ───────────┴────────── SSR(−) / coil(−)
                                S ── GND
  ```

  The two lamp channels have footprints for a flyback diode across the coil (D10 coffee,
  D11 steam), **DNP** by default because the external relay board has its own:

  ```
  5V ──┬──────────────── coil(+)
       K
     SS14 (D10 / D11)
       A
  D ───┴──────────────── coil(−)
  ```

  The 1 kΩ gate resistor is the same value as the switch filters (fewer unique parts);
  switching speed is irrelevant for SSRs and relays. Logic is unchanged: GPIO HIGH turns the load on.
- **Indicator LED:** 1 kΩ + red LED (about 3 mA) across the SSR input or relay coil, so it
  lights exactly when the output is energised.
- **Direct drive** (heater and valve SSRs only) is acceptable only if the SSR datasheet
  guarantees turn-on at ≤ 3.0 V and draws ≤ 12 mA. Measure the input current first. The lamp
  relays always need the MOSFET driver.
- **Heater SSR:** a zero-cross DC-input SSR rated **40 A** (the heater draws about 9–12 A
  at 120 V), on a heatsink. The firmware switches it in time-proportioned 1 s windows.
- **Valve SSR:** a zero-cross DC-input SSR. A small rating is fine, but check the SSR's
  minimum load current: the solenoid draws little, and an SSR's off-state leakage can keep a
  small load humming. Add a bleeder resistor or snubber across the load if so.
- **Lamp relays (coffee, steam):** electromechanical relays with a 5 V, ~30 mA coil (about
  170 Ω) and contacts rated for 120 V AC. A relay has no minimum load current and no
  off-state leakage, so the low-current lamps switch cleanly.
- **Flyback diodes D10 / D11 (DNP):** the external relay board has a flyback diode across
  each coil, right at the coil, which keeps the switching loop short and cuts EMI. Without
  any diode, turning the MOSFET off would produce a spike that can exceed the AO3400A's
  30 V rating. The on-board footprints take an SS14 Schottky (SMA, the same part as D2),
  cathode to +5 V and anode to the MOSFET drain. Fit them only if a relay without its own
  diode is used. The heater and valve channels have no flyback diode because SSR inputs
  are not inductive.
- **GPIO 14 (steam light)** puts out PWM pulses for a moment at boot. The pull-down and the
  relay's pull-in time (several ms) mean this gives a brief click or lamp flicker at worst,
  which is harmless.

### 3.2 Pump dimmer — RobotDyn AC dimmer (PSM = GPIO 25, Z-C = GPIO 34)

- **J6:** 1 = VCC (3.3 V), 2 = GND, 3 = Z-C, 4 = PSM. This is the module's own header
  order, so the cable runs straight through.
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
- **Connector J7 (4-pin JST XH):** 1 = 3.3 V, 2 = Coffee, 3 = Steam, 4 = 3.3 V. Each switch
  gets its own 3.3 V + signal pair (Coffee on pins 1–2, Steam on pins 3–4).
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

### 3.6 Brew pressure — XDB401 via ADS1115 (I²C)

- **Sensor:** XDB401, 0–1.2 MPa (0–12 bar gauge), **5 V supply**, output 0.5–4.5 V.
- **Divider (mandatory):** the sensor's 4.5 V would exceed the ADS1115's input limit
  (VDD + 0.3 V at a 3.3 V supply).

  ```
  XDB401 OUT ── 10 kΩ ──┬── ADS1115 AIN0
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
  - The **100 nF** capacitor at the AIN0 node gives the converter a low-impedance source and
    filters pump pulsation.
  - The divider node is **not** connected to any ESP32 pin (GPIO 36 is a spare).
- **Converter: ADS1115** (PCB U3) on I²C, address 0x48. AIN2 / AIN3 are spare, on J14 (§3.10). 16-bit, ±4.096 V range, 475 SPS
  single-shot. AIN1 measures the 5 V rail through an identical 10k/10k divider about once a
  second; the reading is scaled by 5 V ÷ measured supply, because the XDB401's output is
  ratiometric. No correction if the measured supply is outside 4.5–5.5 V. The machine JSON
  reports the measured supply as `supplyV`.

  An I²C failure makes the reading invalid: shown as such on the dashboard, and a fault
  ("Pressure sensor: ADS1115 not responding") if it lasts 500 ms while brewing.
- **No clamp diode:** the sensor is powered from the board's own 5 V, so its output cannot
  exceed about 5 V, and the 10k/10k divider halves that to about 2.5 V, well inside the
  ADS1115's 0–3.6 V input range. The divider's 5 kΩ source impedance also limits any fault
  current into the ADC's internal protection diodes.
- **Valid range:** the firmware treats a sensor output below 0.35 V (disconnected or
  unpowered) or above 4.75 V as invalid. Invalid for 500 ms while brewing latches a fault;
  in other states the reading is shown but not used.
- **Sampling:** only the control task uses the I²C bus. Every 50 ms it takes one ADS1115
  conversion, smooths the divider voltage (EMA, α = 0.3) and converts it to bar. The
  pressure PID and the dashboard both use that one smoothed value. The pressure is not an
  ESP32 pin, so it does not appear on the IO Status page.

### 3.7 Future: scale — HX711 (DOUT = 35, SCK = 4)

- Power the HX711 at **3.3 V** so DOUT is at 3.3 V logic.
- GPIO 35 is input-only, which is fine because the HX711 drives DOUT itself.
- Holding SCK high for more than 60 µs powers the HX711 down. GPIO 4 floats at boot, so add
  a 10 kΩ pull-down on it.
- Keep the load-cell wiring short and shielded, away from the pump.

### 3.8 I²C bus (SDA = 21, SCL = 22)

- **4.7 kΩ pull-ups to 3.3 V** on SDA and SCL (only once, even with several devices).
- On the PCB: the ADS1115 pressure ADC (U3, address 0x48),
  plus a 4-pin JST XH connector (GND, 3V3, SCL, SDA) for a status OLED or tank-level sensor. Every
  device must run at 3.3 V logic and have a different address.

### 3.9 Spare SPI port — J13 (VSPI: SCK = 18, MOSI = 23, MISO = 19, CS = 15)

Not used by the firmware yet; for a future display, SD card or other SPI device.

| J13 pin | Signal | ESP32 |
|---|---|---|
| 1 | GND | |
| 2 | 3.3 V | |
| 3 | 5 V (supply only, e.g. a display backlight) | |
| 4 | SCK, via 33 Ω | GPIO 18 (shared with the MAX31855) |
| 5 | MOSI, via 33 Ω | GPIO 23 |
| 6 | MISO | GPIO 19 (shared with the MAX31855) |
| 7 | CS, 10 kΩ pull-up to 3.3 V | GPIO 15 |

- **Logic is 3.3 V only.** The 5 V pin is a supply; no signal may be driven to 5 V.
- **Speed:** SCK, MOSI and MISO are the VSPI IO_MUX pins, so the hardware SPI can run up to
  80 MHz (40 MHz is the usual practical limit for a display on a cable). The CS pin can be
  any GPIO without affecting speed.
- **Shared with the thermocouple:** the MAX31855 sits on SCK/MISO with its own CS (GPIO 5). Its
  SO output is high-impedance while its CS is high, so it doesn't load the bus. It's limited to
  5 MHz, so each device needs its own SPI clock setting (Arduino `SPI.beginTransaction`).
- **Firmware needed before use:** the MAX31855 is currently bit-banged on GPIO 18/19. A device
  on J13 needs the hardware VSPI peripheral on those pins, so the thermocouple read must
  move to the same `SPIClass` (at ≤ 5 MHz), with a mutex shared by the control task and
  whoever drives the J13 device.
- **Cable:** the 33 Ω series resistors damp ringing on SCK and MOSI. Keep the cable short
  (< 15 cm) at display speeds.
- **Display extras:** most SPI TFT/OLED displays also need DC (and RST): use J12's GPIO 32 / 33.

### 3.10 Spare analog inputs — J14 (ADS1115 AIN2 / AIN3)

Not read by the firmware yet; for a future analog sensor (e.g. a second pressure sensor,
tank level, or a thermistor divider). The inputs go to U3, the same ADS1115 that reads brew
pressure, so they are not ESP32 pins and don't appear on the IO Status page.

| J14 pin | Signal |
|---|---|
| 1 | GND |
| 2 | 3.3 V |
| 3 | 5 V (sensor supply only) |
| 4 | AIN2 (direct to U3) |
| 5 | AIN3 (direct to U3) |

- **Input range: 0–3.3 V.** The ADS1115 runs at VDD = 3.3 V, and no input may go above
  VDD + 0.3 V. A sensor powered from pin 3 with a 0.5–4.5 V output needs a divider before
  J14, like the 10k/10k on the pressure input (§3.6).
- **No input protection:** AIN2 / AIN3 connect straight to the ADS1115. Never connect a
  5 V signal (or pin 3) to them directly.
- **Noise:** there is no filter capacitor on these inputs. If a sensor's lead is long or runs
  near mains, add 100 nF from the input to GND at the sensor end, or in the divider.
- **Firmware:** reading them is one more ADS1115 single-shot conversion on the same I²C bus,
  done by the control task between pressure samples.

---

## 4. Boot and strapping behaviour

| GPIO | Level required at reset | Use in this design | Why it's safe |
|---|---|---|---|
| 0 | HIGH (LOW = download mode) | Spare (BOOT button) | Unused |
| 2 | LOW or floating | Status LED | LED doesn't pull it high |
| 5 | HIGH | Thermocouple CS | CS idles high; the boot pulses are harmless |
| 12 | LOW (sets flash voltage) | Spare | Unused; never pull it high |
| 15 | HIGH | SPI port CS (J13) | 10 kΩ pull-up holds it HIGH (and devices deselected) |
| 14 | — (pulses at boot) | Steam light relay | At worst a relay click / lamp flicker |

The heater, valve and pump sit on pins (26, 27, 25) with no strapping role and no boot
pulses. The lamps use 13 and 14, where a boot pulse at worst clicks a lamp relay. Every load pin
has a 10 kΩ pull-down. The firmware also drives
them LOW as the very first thing in `setup()`, before Serial or Wi-Fi start.

---

## 5. Bring-up checklist

Do this with the **heater disconnected** or the boiler empty until step 6.

1. Power the low-voltage side only. Check 5 V and 3.3 V, and confirm no GPIO sits above
   3.3 V: in particular Z-C (34) and the switches (16, 17). The pressure divider node
   (ADS1115 AIN0) should read about 0.25 V at 0 bar.
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
