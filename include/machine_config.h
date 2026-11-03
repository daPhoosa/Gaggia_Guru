#pragma once
// ---------------------------------------------------------------------------
// Gaggia Guru - espresso machine control constants.
//
// "Default" values are the factory settings; the user can change them from the
// dashboard Settings page (saved in NVS). Everything else here is a fixed
// safety limit or timing constant and can only be changed by recompiling.
// ---------------------------------------------------------------------------

// ---- User-adjustable defaults ---------------------------------------------
#define DEFAULT_COFFEE_TEMP_C     107.0f
#define DEFAULT_STEAM_TEMP_C      145.0f
#define DEFAULT_BREW_PRESSURE_BAR   6.0f

// Boiler temperature PID (output = heater duty 0-100 %). Starting values only;
// tune on the machine. D acts on the measurement (no kick on setpoint change).
#define DEFAULT_TEMP_KP            6.0f    // % per degC
#define DEFAULT_TEMP_KI            0.3f    // % per degC*s
#define DEFAULT_TEMP_KD           30.0f    // % per degC/s

// Brew pressure PID (output = pump power 0-100 %).
#define DEFAULT_PRESS_KP          10.0f    // % per bar
#define DEFAULT_PRESS_KI           8.0f    // % per bar*s
#define DEFAULT_PRESS_KD           0.0f    // % per bar/s

// Dashboard graph time window (seconds of history shown).
#define DEFAULT_GRAPH_WINDOW_S     60.0f

// Allowed ranges for user settings (the API rejects values outside these).
#define LIMIT_COFFEE_TEMP_MIN      80.0f
#define LIMIT_COFFEE_TEMP_MAX     120.0f
#define LIMIT_STEAM_TEMP_MIN      120.0f
#define LIMIT_STEAM_TEMP_MAX      150.0f   // keep well below SAFETY_MAX_TEMP_C
#define LIMIT_PRESSURE_MIN          1.0f
#define LIMIT_PRESSURE_MAX         11.0f
#define LIMIT_GAIN_MAX           1000.0f
#define LIMIT_GRAPH_WINDOW_MIN     10.0f
#define LIMIT_GRAPH_WINDOW_MAX    600.0f

// ---- Safety limits (fixed) --------------------------------------------------
// Hardware backstop: 165 C manual-reset high-limit thermostat + thermal fuse.
#define SAFETY_MAX_TEMP_C         155.0f   // boiler above this -> FAULT
#define SAFETY_MIN_VALID_TEMP_C     0.0f   // readings outside min..max are treated as bad
#define SAFETY_MAX_VALID_TEMP_C   250.0f
#define SAFETY_TC_FAIL_COUNT          5    // consecutive bad thermocouple reads -> FAULT

// Heater-circuit check: heater driven hard but temperature not rising means
// the high-limit has tripped, the fuse has blown, the element failed or the
// probe came loose. Not evaluated while the pump runs (cold water inflow).
#define HEAT_CHECK_MIN_DUTY        90.0f   // % duty that counts as "heating hard"
#define HEAT_CHECK_WINDOW_MS     120000UL  // must rise within this time...
#define HEAT_CHECK_MIN_RISE_C       2.0f   // ...by at least this much

#define PUMP_MAX_RUN_MS          120000UL  // pump stops; Coffee switch must be cycled
#define ZC_TIMEOUT_MS               500UL  // pump demanded but no zero-cross -> FAULT
#define PRESSURE_FAULT_MS           500UL  // pressure sensor out of range while brewing -> FAULT

// ---- Sensors ----------------------------------------------------------------
// XDB401: 0.5-4.5 V = 0-12 bar (1.2 MPa) at a 5 V supply (ratiometric), through
// a 10k (top) / 10k (bottom) divider. Pin voltage = sensor voltage * 0.5.
#define PRESSURE_DIVIDER_RATIO     0.5f
#define PRESSURE_V_MIN              0.5f   // sensor output at 0 bar
#define PRESSURE_V_MAX              4.5f   // sensor output at full scale
#define PRESSURE_FULL_SCALE_BAR    12.0f
#define PRESSURE_V_VALID_LOW        0.35f  // below = disconnected / unpowered
#define PRESSURE_V_VALID_HIGH       4.75f  // above = shorted / wrong supply

// The pressure divider is read by the ADS1115 (PCB U3) on the I2C bus:
// AIN0 = pressure divider, AIN1 = 5 V rail through an identical divider. The
// sensor is ratiometric, so dividing by the measured supply cancels 5 V drift.
#define ADS1115_I2C_ADDR          0x48     // ADDR pin to GND
#define I2C_CLOCK_HZ           400000UL
#define PRESSURE_SUPPLY_NOMINAL_V   5.0f   // sensor is specified at this supply
#define PRESSURE_SUPPLY_MIN_V       4.5f   // measured supply outside this range:
#define PRESSURE_SUPPLY_MAX_V       5.5f   // ...no correction (measurement suspect)
#define PRESSURE_SUPPLY_PERIOD_MS  1000UL  // re-measure the 5 V rail this often

// ---- Timing -----------------------------------------------------------------
#define CONTROL_PERIOD_MS            50UL  // control task tick
#define TC_READ_PERIOD_MS           200UL  // MAX31855 converts in ~100 ms
#define HEATER_WINDOW_MS           1000UL  // time-proportioning window for the SSR
#define SWITCH_DEBOUNCE_TICKS          2   // consecutive equal samples to accept a switch change
#define READY_BAND_C                2.0f   // "at temperature" light band (+/-)
#define CONTROL_WDT_TIMEOUT_S          3   // control task watchdog -> panic reboot (loads OFF)
