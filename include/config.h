#pragma once

#include "io_pins.h"

// Passwords and Wi-Fi credentials live in include/secrets.h (git-ignored).
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h missing: copy include/secrets.example.h to include/secrets.h and set your passwords"
#endif

// ---------------------------------------------------------------------------
// Project-wide configuration. Edit these before the first flash.
// ---------------------------------------------------------------------------

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// mDNS / ArduinoOTA hostname -> http://gaggia-savant.local
#define DEVICE_HOSTNAME      "gaggia-savant"

// Setup access point started when no network can be joined.
// SSID gets the last 3 MAC bytes appended, e.g. "GaggiaSavant-Setup-A1B2C3".
#define AP_SSID_PREFIX       "GaggiaSavant-Setup-"   // password: AP_PASSWORD in secrets.h

#define WIFI_CONNECT_TIMEOUT_MS   15000UL
#define WIFI_RETRY_INTERVAL_MS    30000UL     // while in AP mode, retry STA this often

// ArduinoOTA (espota / Arduino IDE network port). Password: OTA_PASSWORD in secrets.h.
#define OTA_PORT             3232

// Dashboard live-update period (WebSocket push)
#define STATUS_PUSH_INTERVAL_MS   2000UL

// On-board LED, from io_pins.h (set to -1 to disable)
#define STATUS_LED_PIN       PIN_STATUS_LED
