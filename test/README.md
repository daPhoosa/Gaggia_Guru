# Unit tests (run on the PC)

```sh
pio test -e native                     # all suites
pio test -e native -f test_machine_logic   # one suite
```

Only hardware-free code is compiled here (see `[env:native]` in `platformio.ini`):

| Suite | Covers |
|---|---|
| `test_pid` | `src/pid.h`: P/I/D terms, output clamp, anti-windup, no derivative kick |
| `test_sensor_math` | `src/sensor_math.cpp`: MAX31855 frame decoding and faults, XDB401 voltage → bar, valid range |
| `test_settings` | `src/settings_schema.cpp`: defaults, ranges, text parsing |
| `test_machine_logic` | `src/machine_logic.cpp`: states, switch arming/debounce, outputs, every safety fault |

`test_machine_logic` drives `Machine::Logic` with a small simulator (`Sim`) that steps
time in 50 ms control ticks; nothing touches real pins.
