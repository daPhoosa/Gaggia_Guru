#pragma once
// User-adjustable machine settings (plain data, no Arduino headers).
// Persisted in NVS by settings.cpp; field table and range checks in
// settings_schema.cpp.
struct MachineSettings {
  float coffeeTempC;
  float steamTempC;
  float brewPressureBar;
  float tempKp, tempKi, tempKd;
  float pressKp, pressKi, pressKd;
  float graphWindowS;  // dashboard graph history (display only)
  // New fields go at the END (settings.cpp migrates older saved layouts).
};
