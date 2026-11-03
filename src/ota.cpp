#include "ota.h"

#include <ArduinoOTA.h>
#include <LittleFS.h>

#include "config.h"
#include "machine.h"

namespace {
bool active = false;
bool fsUnmounted = false;  // LittleFS unmounted for a filesystem update
}

namespace Ota {

void begin() {
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setMdnsEnabled(false);  // WifiManager already runs mDNS

  ArduinoOTA
      .onStart([]() {
        active = true;
        Machine::inhibit("firmware update");
        bool fs = ArduinoOTA.getCommand() == U_SPIFFS;
        if (fs) {
          LittleFS.end();  // must unmount before overwriting the partition
          fsUnmounted = true;
        }
        Serial.printf("[OTA] Start updating %s\n", fs ? "filesystem" : "firmware");
      })
      .onEnd([]() { Serial.println("\n[OTA] Done, rebooting"); })
      .onProgress([](unsigned int done, unsigned int total) {
        static unsigned lastPct = 101;
        unsigned pct = done * 100 / total;
        if (pct != lastPct && pct % 10 == 0) Serial.printf("[OTA] %u%%\n", pct);
        lastPct = pct;
      })
      .onError([](ota_error_t err) {
        active = false;
        if (fsUnmounted) {
          // Mount again so the dashboard keeps working (no format; if the
          // partition was partly overwritten this fails and "/" serves the
          // built-in setup page until a good image is uploaded).
          fsUnmounted = false;
          Serial.println(LittleFS.begin(false) ? "[FS] LittleFS remounted"
                                               : "[FS] LittleFS remount failed - upload a filesystem image");
        }
        Machine::release();
        const char *msg = err == OTA_AUTH_ERROR      ? "auth failed"
                          : err == OTA_BEGIN_ERROR   ? "begin failed"
                          : err == OTA_CONNECT_ERROR ? "connect failed"
                          : err == OTA_RECEIVE_ERROR ? "receive failed"
                          : err == OTA_END_ERROR     ? "end failed"
                                                     : "unknown";
        Serial.printf("[OTA] Error %u: %s\n", err, msg);
      });

  ArduinoOTA.begin();
  Serial.printf("[OTA] ArduinoOTA ready on port %d\n", OTA_PORT);
}

void loop() { ArduinoOTA.handle(); }
bool inProgress() { return active; }

}  // namespace Ota
