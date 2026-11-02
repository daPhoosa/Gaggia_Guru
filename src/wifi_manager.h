#pragma once
#include <Arduino.h>

namespace WifiManager {

void begin();
void loop();

bool isConnected();
bool isApMode();
String ip();
String ssid();

// Persist new credentials to NVS. Caller should reboot (or call reconnect()).
bool saveCredentials(const String &ssid, const String &pass);
void clearCredentials();
void reconnect();

// Build a JSON array of nearby networks: [{"ssid":..,"rssi":..,"secure":..},..]
// Returns "[]" + starts an async scan if no results are ready yet.
String scanResultsJson();

}  // namespace WifiManager
