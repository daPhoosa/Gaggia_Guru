#pragma once

// ArduinoOTA: push firmware/filesystem from PlatformIO (espota) or Arduino IDE.
// Browser-based OTA (upload a .bin from the dashboard) lives in web_server.cpp.
namespace Ota {

void begin();
void loop();
bool inProgress();

}  // namespace Ota
