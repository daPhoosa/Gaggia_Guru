#pragma once
// User-adjustable machine settings (plain data, no Arduino headers).
// Persisted in NVS by settings.cpp; field table and range checks in
// settings_schema.cpp.
struct MachineSettings {
  float coffeeTempC;  // brew water target (portafilter); PID aims for this + brewTempOffsetC
  float steamTempC;
  float brewPressureBar;
  float tempKp, tempKi, tempKd;
  float pressKp, pressKi, pressKd;
  float graphWindowS;  // dashboard graph history (display only)
  float graphTempMinC, graphTempMaxC;  // dashboard graph °C axis (display only)
  float graphPressMinBar, graphPressMaxBar;  // dashboard graph bar axis (display only)
  float brewTempOffsetC;  // thermocouple minus portafilter water temp (coffee mode only)
  // New fields go at the END (settings.cpp migrates older saved layouts).
};
