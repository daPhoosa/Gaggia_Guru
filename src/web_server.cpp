#include "web_server.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "config.h"
#include "io_status.h"
#include "machine.h"
#include "ota.h"
#include "settings.h"
#include "wifi_manager.h"

namespace {

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

uint32_t rebootAt = 0;  // millis() deadline for a deferred restart (0 = none)
uint32_t lastPush = 0;
uint32_t lastMachinePush = 0;
constexpr uint32_t kMachinePushMs = 250;
bool fsUnmounted = false;  // LittleFS was unmounted for a filesystem upload

// After a failed/aborted filesystem upload, mount LittleFS again so the
// dashboard keeps working. No format: if the partition was already partly
// overwritten the mount fails, and "/" falls back to the built-in setup page
// (with its link to /update) until a good image is uploaded.
void remountFs() {
  if (!fsUnmounted) return;
  fsUnmounted = false;
  Serial.println(LittleFS.begin(false) ? "[FS] LittleFS remounted"
                                       : "[FS] LittleFS remount failed - upload a filesystem image");
}

void scheduleReboot(uint32_t delayMs = 1000) {
  Machine::inhibit("rebooting");  // loads off before the restart
  rebootAt = millis() + delayMs;
}

// Host header without any ":port" suffix
String hostOnly(AsyncWebServerRequest *req) {
  String h = req->host();
  int colon = h.indexOf(':');
  return colon < 0 ? h : h.substring(0, colon);
}

// Names this device answers to: its IP addresses, the mDNS name, the bare
// hostname, and the hostname under the usual private home-router DNS suffixes.
// Deliberately NOT "gaggia-guru.<anything>", since anyone can register e.g.
// gaggia-guru.example.com and point it at this device (DNS rebinding).
bool isOwnName(const String &host) {
  if (host == WiFi.softAPIP().toString()) return true;
  if (WifiManager::isConnected() && host == WiFi.localIP().toString()) return true;
  static const char *const kNames[] = {
      DEVICE_HOSTNAME,           DEVICE_HOSTNAME ".local", DEVICE_HOSTNAME ".lan",
      DEVICE_HOSTNAME ".home",   DEVICE_HOSTNAME ".home.arpa",
      DEVICE_HOSTNAME ".localdomain", DEVICE_HOSTNAME ".internal",
  };
  for (const char *n : kNames)
    if (host.equalsIgnoreCase(n)) return true;
  return false;
}

// Captive portal: a request with no Host header is also treated as ours.
bool isOwnHost(const String &host) { return host.isEmpty() || isOwnName(host); }

bool requireAdmin(AsyncWebServerRequest *req) {
  if (req->authenticate(ADMIN_USER, ADMIN_PASSWORD)) return true;
  req->requestAuthentication();
  return false;
}

// Cross-site request guard for every state-changing POST. Without it, any web
// page open in a browser that has the admin login cached could submit a form
// to the machine (change setpoints, wipe Wi-Fi, reboot, flash firmware).
//  - Our own pages send the custom header "X-Guru: 1". A page on another
//    site can't add a custom header to a request to us without a CORS
//    preflight, which this server never approves.
//  - The plain-HTML /update fallback page can't add headers, so a POST whose
//    Origin is exactly this host is accepted too (browsers set Origin on POST).
bool sameSite(AsyncWebServerRequest *req) {
  // The requested name must be one of ours. This blocks DNS rebinding, where a
  // foreign site's name resolves to this device so the browser treats the
  // machine as that site (same origin, custom headers allowed).
  if (!isOwnName(hostOnly(req))) return false;
  if (req->hasHeader("X-Guru")) return true;
  if (!req->hasHeader("Origin")) return false;
  return req->header("Origin").equalsIgnoreCase("http://" + req->host());
}

void sendForbidden(AsyncWebServerRequest *req) {
  req->send(403, "application/json", "{\"ok\":false,\"error\":\"cross-site request refused\"}");
}

// true = request may proceed; otherwise a 403 has already been sent.
bool requireSameSite(AsyncWebServerRequest *req) {
  if (sameSite(req)) return true;
  Serial.printf("[HTTP] Refused cross-site POST %s (Host: %s, Origin: %s)\n", req->url().c_str(),
                req->host().c_str(), req->hasHeader("Origin") ? req->header("Origin").c_str() : "none");
  sendForbidden(req);
  return false;
}

String statusJson() {
  JsonDocument doc;
  doc["fw"] = FW_VERSION;
  doc["build"] = __DATE__ " " __TIME__;
  doc["hostname"] = DEVICE_HOSTNAME;
  doc["uptime"] = millis() / 1000;
  doc["heapFree"] = ESP.getFreeHeap();
  doc["heapMin"] = ESP.getMinFreeHeap();
  doc["heapSize"] = ESP.getHeapSize();
  doc["chip"] = ESP.getChipModel();
  doc["chipRev"] = ESP.getChipRevision();
  doc["cpuMHz"] = ESP.getCpuFreqMHz();
  doc["flashSize"] = ESP.getFlashChipSize();
  doc["sketchSize"] = ESP.getSketchSize();
  doc["sketchFree"] = ESP.getFreeSketchSpace();
  doc["fsUsed"] = LittleFS.usedBytes();
  doc["fsTotal"] = LittleFS.totalBytes();
  doc["partition"] = esp_ota_get_running_partition()->label;
  doc["mac"] = WiFi.macAddress();
  doc["apMode"] = WifiManager::isApMode();
  doc["connected"] = WifiManager::isConnected();
  doc["ssid"] = WifiManager::ssid();
  doc["ip"] = WifiManager::ip();
  doc["rssi"] = WifiManager::isConnected() ? WiFi.RSSI() : 0;
  doc["clients"] = ws.count();
  String out;
  serializeJson(doc, out);
  return out;
}

// ---- Browser OTA upload -----------------------------------------------------
// POST /api/update?target=firmware|fs   (multipart, field name "file")
//
// Only one update at a time: the flash writer (Update) is a single global, and
// ArduinoOTA uses it too. While an upload runs, a second one gets 409 Busy
// before it touches anything (no inhibit, no unmount, no Update calls).
//
// Per-request results live in req->_tempObject (freed by the library when the
// request ends). The global state only tracks which request owns the update.

enum class UploadState : uint8_t { Idle, Running, Done };  // Done = success, reboot pending
UploadState uploadState = UploadState::Idle;
AsyncWebServerRequest *uploadOwner = nullptr;  // valid while Running

struct UploadCtx {
  uint16_t status;   // 0 = proceeding/ok, else HTTP status to answer (401/403/409/500)
  bool received;     // a file part arrived in this request
  char error[72];
};

UploadCtx *uploadCtx(AsyncWebServerRequest *req) {
  if (!req->_tempObject) req->_tempObject = calloc(1, sizeof(UploadCtx));
  return static_cast<UploadCtx *>(req->_tempObject);
}

void setUploadError(UploadCtx *c, uint16_t status, const char *msg) {
  c->status = status;
  strlcpy(c->error, msg, sizeof(c->error));
}

// End a running upload that did not succeed: discard the partial image, mount
// the filesystem again, resume machine control and free the slot.
void failUpload(const char *why) {
  Serial.printf("[WebOTA] Error: %s\n", why);
  Update.abort();
  remountFs();
  uploadState = UploadState::Idle;
  uploadOwner = nullptr;
  Machine::release();
}

void handleUpdateUpload(AsyncWebServerRequest *req, const String &filename, size_t index,
                        uint8_t *data, size_t len, bool final) {
  UploadCtx *c = uploadCtx(req);
  if (!c) return;  // out of memory; handleUpdateDone reports it

  if (index == 0) {
    if (c->received) {  // a second file in the same multipart request
      if (uploadOwner == req && uploadState == UploadState::Running) failUpload("more than one file");
      if (!c->status) setUploadError(c, 400, "send one file per upload");
      return;
    }
    c->received = true;
    if (!sameSite(req)) return setUploadError(c, 403, "cross-site request refused");
    if (!req->authenticate(ADMIN_USER, ADMIN_PASSWORD)) return setUploadError(c, 401, "unauthorized");
    if (uploadState != UploadState::Idle || Ota::inProgress()) {
      Serial.println("[WebOTA] Refused: another update is in progress");
      return setUploadError(c, 409, "another update is in progress");
    }

    uploadState = UploadState::Running;
    uploadOwner = req;
    // Connection dropped mid-upload (browser closed, Wi-Fi lost): clean up so
    // the machine doesn't stay inhibited and the next upload isn't refused.
    req->onDisconnect([req]() {
      if (uploadState == UploadState::Running && uploadOwner == req) failUpload("connection lost");
    });

    Machine::inhibit("firmware upload");
    const bool fs = req->hasParam("target") && req->getParam("target")->value() == "fs";
    Serial.printf("[WebOTA] Receiving %s: %s\n", fs ? "filesystem" : "firmware", filename.c_str());
    if (fs) {
      LittleFS.end();  // must unmount before overwriting the partition
      fsUnmounted = true;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, fs ? U_SPIFFS : U_FLASH)) {
      setUploadError(c, 500, Update.errorString());
      failUpload(c->error);
      return;
    }
  }
  if (c->status || uploadOwner != req || uploadState != UploadState::Running) return;

  if (len && Update.write(data, len) != len) {
    setUploadError(c, 500, Update.errorString());
    failUpload(c->error);
    return;
  }
  if (final) {
    if (Update.end(true)) {
      Serial.printf("[WebOTA] Success, %u bytes\n", index + len);
      uploadState = UploadState::Done;  // stays busy until the reboot
      uploadOwner = nullptr;
      scheduleReboot(1500);             // even if the browser never reads the reply
    } else {
      setUploadError(c, 500, Update.errorString());
      failUpload(c->error);
    }
  }
}

void sendUploadResult(AsyncWebServerRequest *req, uint16_t code, const char *error) {
  JsonDocument doc;
  doc["ok"] = code == 200;
  if (error) doc["error"] = error;
  String out;
  serializeJson(doc, out);
  AsyncWebServerResponse *res = req->beginResponse(code, "application/json", out);
  res->addHeader("Connection", "close");
  req->send(res);
}

void handleUpdateDone(AsyncWebServerRequest *req) {
  UploadCtx *c = static_cast<UploadCtx *>(req->_tempObject);
  // Body ended without the final chunk being written (shouldn't happen): don't leave it running.
  if (uploadState == UploadState::Running && uploadOwner == req) failUpload("upload incomplete");

  if (!requireSameSite(req)) return;
  if ((c && c->status == 401) || !req->authenticate(ADMIN_USER, ADMIN_PASSWORD)) {
    req->requestAuthentication();
    return;
  }
  if (!c) return sendUploadResult(req, 400, "no file uploaded");
  if (c->status) return sendUploadResult(req, c->status, c->error);
  if (!c->received) return sendUploadResult(req, 400, "no file uploaded");
  sendUploadResult(req, 200, nullptr);  // reboot already scheduled
}

// ---- Captive portal ---------------------------------------------------------
// Built-in Wi-Fi setup page (in firmware, so provisioning works without LittleFS).
const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Wi-Fi setup · Gaggia Guru</title><style>
body{font:16px system-ui,sans-serif;max-width:420px;margin:0 auto;padding:16px;background:#f4f5f7;color:#1d2330}
input,select,button{width:100%;box-sizing:border-box;font:inherit;padding:10px;margin:6px 0;border:1px solid #ccc;border-radius:7px}
button{background:#2563eb;color:#fff;border:0;font-weight:600}#msg{margin-top:10px}a{color:#2563eb}
</style></head><body><h2>Gaggia Guru Wi-Fi setup</h2>
<form id=f><select id=s><option value="">Scanning…</option></select>
<input name=ssid id=ssid placeholder="SSID" required maxlength=32>
<input name=pass type=password placeholder="Password" maxlength=64>
<button>Save &amp; reboot</button></form><div id=msg></div>
<p><a href="/">Full dashboard</a> · <a href="/update">Firmware update</a></p>
<script>
const s=document.getElementById('s'),ssid=document.getElementById('ssid');
async function scan(n){const l=await fetch('/api/wifi/scan').then(r=>r.json()).catch(()=>[]);
 if(!l.length&&n<10)return setTimeout(()=>scan(n+1),1000);
 const m=new Map();l.forEach(x=>{if(x.ssid&&(!m.has(x.ssid)||m.get(x.ssid).rssi<x.rssi))m.set(x.ssid,x)});
 s.innerHTML='<option value="">Choose a network…</option>';
 [...m.values()].sort((a,b)=>b.rssi-a.rssi).forEach(x=>{const o=new Option((x.secure?'🔒 ':'')+x.ssid+' ('+x.rssi+' dBm)',x.ssid);s.add(o)});}
s.onchange=()=>{if(s.value)ssid.value=s.value};scan(0);
document.getElementById('f').onsubmit=async e=>{e.preventDefault();
 const r=await fetch('/api/wifi',{method:'POST',headers:{'X-Guru':'1'},body:new URLSearchParams(new FormData(e.target))}).catch(()=>null);
 document.getElementById('msg').textContent=r&&r.ok?'Saved. The device is rebooting and will join that network. Reconnect your phone to your normal Wi-Fi and browse to http://)HTML" DEVICE_HOSTNAME R"HTML(.local':'Save failed';};
</script></body></html>)HTML";

void sendSetupPage(AsyncWebServerRequest *req) {
  AsyncWebServerResponse *res = req->beginResponse(200, "text/html", SETUP_HTML);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

// While the setup AP is up, any request for a foreign host (connectivitycheck.gstatic.com,
// captive.apple.com, www.msftconnecttest.com, ...) is redirected to our own IP. This makes
// phones/laptops show their "Sign in to network" page instead of an error.
class CaptivePortalHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *req) const override {
    return WifiManager::isApMode() && !isOwnHost(hostOnly(req));
  }
  void handleRequest(AsyncWebServerRequest *req) override {
    AsyncWebServerResponse *res = req->beginResponse(302, "text/plain", "");
    res->addHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  }
};

void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *,
               uint8_t *, size_t) {
  if (type == WS_EVT_CONNECT) {
    client->text(statusJson());
    client->text(Machine::json());
  }
}

}  // namespace

namespace WebUi {

void begin() {
  if (!LittleFS.begin(true)) Serial.println("[FS] LittleFS mount failed");

  // Must be registered first so it wins over every other route while in AP mode.
  server.addHandler(new CaptivePortalHandler());

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // --- JSON API ---
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", statusJson());
  });

  server.on("/api/io", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", IoStatus::json());
  });

  server.on("/api/machine", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", Machine::json());
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", Settings::json());
  });

  // Form fields named after the setting keys; only the fields sent are changed.
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!requireSameSite(req) || !requireAdmin(req)) return;
    MachineSettings s = Settings::get();
    String err;
    size_t changed = 0;
    for (size_t i = 0; i < Settings::fieldCount(); ++i) {
      const char *key = Settings::fieldKey(i);
      if (!req->hasParam(key, true)) continue;
      if (!Settings::setField(s, key, req->getParam(key, true)->value(), err)) break;
      ++changed;
    }
    if (err.isEmpty() && !changed) err = "no settings given";
    if (err.isEmpty()) Settings::set(s, err);  // validates again and saves to NVS
    if (!err.isEmpty()) {
      JsonDocument doc;
      doc["ok"] = false;
      doc["error"] = err;
      String out;
      serializeJson(doc, out);
      req->send(400, "application/json", out);
      return;
    }
    req->send(200, "application/json", Settings::json());
  });

  server.on("/api/settings/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!requireSameSite(req) || !requireAdmin(req)) return;
    Settings::resetToDefaults();
    req->send(200, "application/json", Settings::json());
  });

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", WifiManager::scanResultsJson());
  });

  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *req) {
    // Open while in setup-AP mode so first-time provisioning works; otherwise admin only.
    if (!requireSameSite(req)) return;
    if (!WifiManager::isApMode() && !requireAdmin(req)) return;
    if (!req->hasParam("ssid", true)) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
      return;
    }
    String ssid = req->getParam("ssid", true)->value();
    String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";
    if (!WifiManager::saveCredentials(ssid, pass)) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid credentials\"}");
      return;
    }
    req->send(200, "application/json", "{\"ok\":true}");
    scheduleReboot(1500);
  });

  server.on("/api/wifi/forget", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!requireSameSite(req) || !requireAdmin(req)) return;
    WifiManager::clearCredentials();
    req->send(200, "application/json", "{\"ok\":true}");
    scheduleReboot(1500);
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!requireSameSite(req) || !requireAdmin(req)) return;
    req->send(200, "application/json", "{\"ok\":true}");
    scheduleReboot(1000);
  });

  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);

  // Fallback page so the device is still updatable if the filesystem is empty.
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!requireAdmin(req)) return;
    req->send(200, "text/html",
              "<!doctype html><meta name=viewport content='width=device-width'>"
              "<h3>Firmware update</h3>"
              "<form method=POST action='/api/update?target=firmware' enctype=multipart/form-data>"
              "<input type=file name=file accept=.bin> <button>Upload firmware</button></form><br>"
              "<form method=POST action='/api/update?target=fs' enctype=multipart/form-data>"
              "<input type=file name=file accept=.bin> <button>Upload filesystem</button></form>");
  });

  server.on("/setup", HTTP_GET, sendSetupPage);

  // Root: dashboard if the filesystem was uploaded, otherwise the built-in setup page.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/index.html")) {
      req->send(LittleFS, "/index.html", "text/html");
    } else {
      sendSetupPage(req);
    }
  });

  // --- Static dashboard assets from LittleFS (data/ folder) ---
  // no-cache: browsers revalidate, so pages update right after an OTA filesystem upload.
  server.serveStatic("/", LittleFS, "/").setCacheControl("no-cache");

  // Unknown URLs on our own host: send the browser to the root page.
  server.onNotFound([](AsyncWebServerRequest *req) {
    if (req->url().startsWith("/api/")) {
      req->send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
    } else {
      req->redirect("/");
    }
  });

  // No Access-Control-Allow-Origin header: the dashboard is served from this
  // device, and other sites must not be able to read the API or pass a preflight.
  server.begin();
  Serial.println("[HTTP] Dashboard on port 80");
}

void loop() {
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    Serial.println("[SYS] Rebooting...");
    delay(100);
    ESP.restart();
  }
  if (millis() - lastPush >= STATUS_PUSH_INTERVAL_MS) {
    lastPush = millis();
    ws.cleanupClients();
    if (ws.count()) ws.textAll(statusJson());
  }
  if (millis() - lastMachinePush >= kMachinePushMs) {
    lastMachinePush = millis();
    if (ws.count()) ws.textAll(Machine::json());
  }
}

}  // namespace WebUi
