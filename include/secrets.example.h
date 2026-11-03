#pragma once
// ---------------------------------------------------------------------------
// Gaggia Savant - passwords and credentials (TEMPLATE).
//
// Copy this file to include/secrets.h and set your own values:
//     cp include/secrets.example.h include/secrets.h
// secrets.h is listed in .gitignore, so real passwords never get committed.
// The OTA upload (pio run -e esp32-ota) reads OTA_PASSWORD from secrets.h too,
// via scripts/ota_auth.py, so it only needs to be set here.
// ---------------------------------------------------------------------------

// HTTP basic auth for the dashboard's settings, Wi-Fi, reboot and update.
#define ADMIN_USER           "admin"
#define ADMIN_PASSWORD       "changeme"

// ArduinoOTA (espota / Arduino IDE network port).
#define OTA_PASSWORD         "changeme-ota"

// Setup access point password: >= 8 chars, or "" for an open AP.
#define AP_PASSWORD          "configure-me"

// Optional compile-time Wi-Fi credentials. Used only if none are saved in NVS.
// Leave empty to rely on the setup access point / dashboard Wi-Fi page.
#define DEFAULT_WIFI_SSID    ""
#define DEFAULT_WIFI_PASS    ""
