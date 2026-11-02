#pragma once

namespace WebUi {

void begin();
void loop();  // pushes status over WebSocket, handles deferred reboot

}  // namespace WebUi
