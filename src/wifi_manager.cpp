#include "wifi_manager.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "config.h"

namespace {

Preferences prefs;
DNSServer dns;
bool apMode = false;
uint32_t lastRetry = 0;
String storedSsid, storedPass;

void loadCredentials() {
  prefs.begin("wifi", true);
  storedSsid = prefs.getString("ssid", DEFAULT_WIFI_SSID);
  storedPass = prefs.getString("pass", DEFAULT_WIFI_PASS);
  prefs.end();
}

bool connectSta(uint32_t timeoutMs) {
  if (storedSsid.isEmpty()) return false;
  Serial.printf("[WiFi] Connecting to \"%s\"", storedSsid.c_str());
  WiFi.begin(storedSsid.c_str(), storedPass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected, IP %s, RSSI %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  Serial.println("[WiFi] Connection failed");
  return false;
}

void startAp() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char apSsid[32];
  snprintf(apSsid, sizeof(apSsid), "%s%02X%02X%02X", AP_SSID_PREFIX, mac[3], mac[4], mac[5]);

  WiFi.mode(WIFI_AP_STA);  // keep STA so we can scan / retry
  WiFi.softAP(apSsid, strlen(AP_PASSWORD) >= 8 ? AP_PASSWORD : nullptr);
  dns.start(53, "*", WiFi.softAPIP());  // captive portal: resolve everything to us
  apMode = true;
  lastRetry = millis();
  Serial.printf("[WiFi] Setup AP \"%s\" started, browse to http://%s\n", apSsid,
                WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apMode = false;
  Serial.println("[WiFi] Setup AP stopped");
}

}  // namespace

namespace WifiManager {

void begin() {
  WiFi.persistent(false);  // we manage credentials in our own NVS namespace
  // Hostname must be set before the STA interface starts (WiFi.mode), or the
  // DHCP request goes out as the default "esp32-XXXXXX" and routers list that.
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  loadCredentials();

  if (!connectSta(WIFI_CONNECT_TIMEOUT_MS)) startAp();

  if (MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] http://%s.local\n", DEVICE_HOSTNAME);
  }
}

void loop() {
  if (!apMode) return;
  dns.processNextRequest();

  // While serving the setup AP, periodically try the saved network again,
  // but not while a client is connected to the AP (channel hopping drops them).
  if (WiFi.status() == WL_CONNECTED) {
    stopAp();
    return;
  }
  if (!storedSsid.isEmpty() && WiFi.softAPgetStationNum() == 0 &&
      millis() - lastRetry > WIFI_RETRY_INTERVAL_MS) {
    lastRetry = millis();
    Serial.println("[WiFi] Retrying saved network...");
    WiFi.begin(storedSsid.c_str(), storedPass.c_str());
  }
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }
bool isApMode() { return apMode; }
String ip() { return isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
String ssid() { return isConnected() ? WiFi.SSID() : String(); }

bool saveCredentials(const String &ssid, const String &pass) {
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 64) return false;
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  storedSsid = ssid;
  storedPass = pass;
  Serial.printf("[WiFi] Saved credentials for \"%s\"\n", ssid.c_str());
  return true;
}

void clearCredentials() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  storedSsid = "";
  storedPass = "";
}

void reconnect() {
  WiFi.disconnect();
  WiFi.begin(storedSsid.c_str(), storedPass.c_str());
}

String scanResultsJson() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);  // async
    return "[]";
  }
  if (n == WIFI_SCAN_RUNNING) return "[]";

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  WiFi.scanNetworks(true);  // kick off a fresh scan for next request
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace WifiManager
