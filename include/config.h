#pragma once

#include "io_pins.h"

// ---------------------------------------------------------------------------
// Project-wide configuration. Edit these before the first flash.
// ---------------------------------------------------------------------------

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// mDNS / ArduinoOTA hostname -> http://gaggia-savant.local
#define DEVICE_HOSTNAME      "gaggia-savant"

// Optional compile-time Wi-Fi credentials. Used only if none are saved in NVS.
// Leave empty to rely on the setup access point / dashboard Wi-Fi page.
#define DEFAULT_WIFI_SSID    ""
#define DEFAULT_WIFI_PASS    ""

// Setup access point started when no network can be joined.
// SSID gets the last 3 MAC bytes appended, e.g. "GaggiaSavant-Setup-A1B2C3".
#define AP_SSID_PREFIX       "GaggiaSavant-Setup-"
#define AP_PASSWORD          "configure-me"   // >= 8 chars, or "" for open AP

#define WIFI_CONNECT_TIMEOUT_MS   15000UL
#define WIFI_RETRY_INTERVAL_MS    30000UL     // while in AP mode, retry STA this often

// ArduinoOTA (espota / Arduino IDE network port). Must match platformio.ini --auth.
#define OTA_PASSWORD         "changeme-ota"
#define OTA_PORT             3232

// HTTP basic auth protecting firmware upload, Wi-Fi changes and reboot.
#define ADMIN_USER           "admin"
#define ADMIN_PASSWORD       "changeme"

// Dashboard live-update period (WebSocket push)
#define STATUS_PUSH_INTERVAL_MS   2000UL

// On-board LED, from io_pins.h (set to -1 to disable)
#define STATUS_LED_PIN       PIN_STATUS_LED
