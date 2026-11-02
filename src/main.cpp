// Gaggia Savant - ESP32 control system for a converted Gaggia Classic Pro
//  - Wi-Fi station with fallback setup AP + captive portal
//  - OTA updates: ArduinoOTA (espota) and browser upload (/api/update)
//  - Async web server serving a LittleFS dashboard with live WebSocket status
//  - Machine control (src/machine.*) runs in its own task, independent of the above

#include <Arduino.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "io_status.h"
#include "machine.h"
#include "ota.h"
#include "web_server.h"
#include "wifi_manager.h"

// ---------------------------------------------------------------------------
static void updateStatusLed() {
#if STATUS_LED_PIN >= 0
  // Solid = connected, fast blink = setup AP, slow blink = connecting
  uint32_t period = WifiManager::isConnected() ? 0 : WifiManager::isApMode() ? 200 : 1000;
  digitalWrite(STATUS_LED_PIN, period == 0 ? HIGH : (millis() / period) % 2);
#endif
}

// If this is the first boot after an OTA update, confirm the image is good so the
// bootloader doesn't roll back (only matters when rollback is enabled in sdkconfig).
static void confirmOtaImage() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[OTA] New firmware marked valid");
  }
}

void setup() {
  Machine::safeOutputs();  // FIRST: heater, pump, valve and lights OFF
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n\n=== %s v%s (%s %s) ===\n", DEVICE_HOSTNAME, FW_VERSION, __DATE__, __TIME__);
  Serial.printf("Running from partition: %s\n", esp_ota_get_running_partition()->label);

#if STATUS_LED_PIN >= 0
  pinMode(STATUS_LED_PIN, OUTPUT);
#endif

  Machine::begin();        // control starts before the (blocking) Wi-Fi connect
  WifiManager::begin();
  Ota::begin();
  IoStatus::begin();
  WebUi::begin();
  confirmOtaImage();
}

void loop() {
  WifiManager::loop();
  Ota::loop();
  if (Ota::inProgress()) return;  // keep the CPU on the update
  WebUi::loop();
  updateStatusLed();
  IoStatus::loop();
}
