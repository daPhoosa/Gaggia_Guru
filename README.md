# Gaggia Savant

ESP32-WROOM-32U control system for a converted Gaggia Classic Pro espresso machine.
PlatformIO / Arduino firmware with:

- **Wi-Fi manager** – joins a saved network; if none or it fails, starts a setup AP
  (`GaggiaSavant-Setup-XXXXXX`) with a captive portal so you can enter credentials from a phone.
  Credentials are kept in NVS and survive firmware updates.
- **OTA updates, two ways**
  - **ArduinoOTA / espota** – `pio run -e esp32-ota -t upload` from your PC.
  - **Browser upload** – Update tab on the dashboard, or the always-available
    fallback page at `/update` (works even if the filesystem is empty).
- **Async web server** serving a dashboard from LittleFS (`data/`) with live status
  over WebSocket (uptime, heap, RSSI, flash/FS usage, boot partition…).
- **mDNS** – `http://gaggia-savant.local`

> The **-32U** has no PCB antenna — connect an external 2.4 GHz antenna to the u.FL
> connector before powering up Wi-Fi.

## Layout

```
platformio.ini        build envs: esp32-serial (USB) and esp32-ota (Wi-Fi)
partitions.csv        4MB: 2 × 1.5MB OTA app slots, 896KB LittleFS, coredump
include/config.h      hostname, passwords, AP name, timings  ← edit first
include/io_pins.h     every GPIO: '#define PIN_X n // Name | Type | notes'
scripts/gen_io_pins.py  pre-build: io_pins.h comments → include/io_pins_table.gen.h
src/main.cpp          setup/loop + appSetup()/appLoop() hooks for your code
src/wifi_manager.*    STA connect, fallback AP, captive DNS, scan, NVS creds
src/ota.*             ArduinoOTA
src/web_server.*      HTTP routes, JSON API, WebSocket, browser OTA
src/io_status.*       read-only pin sampling for /api/io
data/                 web UI → LittleFS: index.html (dashboard), wifi.html,
                      update.html, io.html, shared style.css + app.js
```

## First flash (USB)

1. Edit `include/config.h` — change `OTA_PASSWORD`, `ADMIN_PASSWORD`, `AP_PASSWORD`.
   Keep `--auth` in `platformio.ini` in sync with `OTA_PASSWORD`.
   Optionally set `DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASS`.
2. Flash firmware and the dashboard filesystem:
   ```sh
   pio run -e esp32-serial -t upload
   pio run -e esp32-serial -t uploadfs
   pio device monitor
   ```
3. If no Wi-Fi is configured, join `GaggiaSavant-Setup-XXXXXX` (password `AP_PASSWORD`),
   browse to `http://192.168.4.1`, open **Wi-Fi**, pick a network, Save. The device reboots
   onto your network.

## Updating over Wi-Fi

```sh
pio run -e esp32-ota -t upload      # firmware
pio run -e esp32-ota -t uploadfs    # dashboard files
```
If `gaggia-savant.local` doesn't resolve, set `upload_port` in `platformio.ini` to the IP.

Or from the browser: **Update** tab → choose `.pio/build/esp32-serial/firmware.bin`
(or `littlefs.bin` from `pio run -t buildfs`) → Upload. Login is `ADMIN_USER` / `ADMIN_PASSWORD`.

Bump `-DFW_VERSION` in `platformio.ini` to see the new version on the dashboard.

## HTTP API

| Method | Path                           | Auth*  | Description                          |
|--------|--------------------------------|--------|--------------------------------------|
| GET    | `/api/status`                  | –      | Device status JSON                   |
| WS     | `/ws`                          | –      | Status JSON pushed every 2 s         |
| GET    | `/api/io`                      | –      | Pin names/types (from io_pins.h) + live state |
| GET    | `/api/wifi/scan`               | –      | Nearby networks (async; poll)        |
| POST   | `/api/wifi` (`ssid`, `pass`)   | admin† | Save credentials and reboot          |
| POST   | `/api/wifi/forget`             | admin  | Clear credentials and reboot         |
| POST   | `/api/reboot`                  | admin  | Reboot                               |
| POST   | `/api/update?target=firmware\|fs` | admin | Multipart `.bin` upload, reboots on success |
| GET    | `/update`                      | admin  | Minimal fallback upload page         |

\* HTTP Basic auth. † Open while in setup-AP mode so first-time provisioning works.

## IO pin map

`include/io_pins.h` lists every usable GPIO of the ESP32-WROOM-32U in 38-pin board order.
Each active line must be:

```c
#define PIN_RELAY_1   4   // RELAY_1 | Digital | drives K1 via Q1
```

- **Type** is one of `Analog`, `Digital`, `Communications`, `Unused`.
- Lines starting with `//` are ignored (flash pins GPIO6–11, and GPIO37/38 which the module lacks).
- `scripts/gen_io_pins.py` parses the comments on every build and fails the build with a
  clear message on a bad type, duplicate GPIO, flash pin or missing comment.
- The dashboard's **IO Status** page shows each pin's name, type, mode (IN/OUT/PERIPH/ANALOG),
  pull-up/down and level (HIGH/LOW), refreshed every second. Reading is non-invasive; only
  pins typed `Analog` are sampled with `analogRead()`. ADC2 pins can't be read while Wi-Fi is on —
  use ADC1 (GPIO32–39) for analog inputs.

## Adding your own features

- Put peripheral init in `appSetup()` and non-blocking logic in `appLoop()` (`src/main.cpp`).
- Add fields to `statusJson()` in `src/web_server.cpp` and render them in `data/app.js`
  — they're pushed to the dashboard automatically.
- Add routes in `WebUi::begin()`.

## Notes

- Basic auth over plain HTTP is not encrypted; keep the device on a trusted network.
- `STATUS_LED_PIN` defaults to GPIO2 (common on dev boards); set `-1` if your board differs.
- Flash size: WROOM-32U modules come in 4/8/16MB. This project assumes 4MB; for larger
  modules, change `board_build.flash_size` and enlarge the partitions.

## Troubleshooting

- **Android Chrome can't open the dashboard (Firefox can):** newer Android versions require apps
  to have permission to reach local-network devices. Settings → Apps → Chrome → Permissions →
  **Nearby devices** (a.k.a. *Local network*) → Allow.
- **`pio run -e esp32-ota -t upload` stops at "No response from device":** the ESP32 connects
  back to the PC on a TCP host port (3233–3240, picked per upload by `scripts/espota_port.py`). Allow the range in the PC firewall, e.g.
  `sudo ufw allow from 192.168.1.0/24 to any port 3233:3240 proto tcp`.
