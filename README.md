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
IO_design.md          pin assignments, wiring and signal conditioning (hardware reference)
platformio.ini        build envs: esp32-serial (USB) and esp32-ota (Wi-Fi)
partitions.csv        4MB: 2 × 1.5MB OTA app slots, 896KB LittleFS, coredump
include/secrets.h     passwords, optional Wi-Fi creds (git-ignored; copy from secrets.example.h)  ← edit first
include/config.h      hostname, AP name, ports, timings
include/machine_config.h  setpoint/PID defaults, safety limits, control timing
include/io_pins.h     every GPIO: '#define PIN_X n // Name | Type | notes'
scripts/gen_io_pins.py  pre-build: io_pins.h comments → include/io_pins_table.gen.h
scripts/ota_auth.py   esp32-ota upload: takes the OTA password from secrets.h
src/main.cpp          setup/loop; loads forced OFF first, machine control started before Wi-Fi
src/machine.*         control task (own FreeRTOS task + watchdog): pin I/O, logging, status publishing
src/machine_logic.*   state machine, heater/pump PID, safety faults (hardware-free, unit-tested)
src/sensors.*         MAX31855K thermocouple, XDB401 pressure sensor (pin I/O)
src/sensor_math.*     MAX31855 frame decoding, voltage -> bar (hardware-free, unit-tested)
src/pump.*            RobotDyn dimmer: zero-cross ISR, full-cycle pulse-skip modulation
src/settings.*        user settings (setpoints, PID gains) in NVS
src/settings_schema.* settings field table, defaults, range checks (hardware-free, unit-tested)
src/pid.h             PID controller
src/wifi_manager.*    STA connect, fallback AP, captive DNS, scan, NVS creds
src/ota.*             ArduinoOTA
src/web_server.*      HTTP routes, JSON API, WebSocket, browser OTA
src/io_status.*       read-only pin sampling for /api/io
test/                 PC unit tests (pio test -e native), see test/README.md
data/                 web UI → LittleFS: index.html (dashboard), settings.html, wifi.html,
                      update.html, io.html, shared style.css + app.js
```

## First flash (USB)

1. Create your secrets file and set real passwords in it (`OTA_PASSWORD`, `ADMIN_PASSWORD`,
   `AP_PASSWORD`, optionally `DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASS`):
   ```sh
   cp include/secrets.example.h include/secrets.h
   ```
   `include/secrets.h` is git-ignored; the build stops with an error if it is missing.
   The Wi-Fi upload reads `OTA_PASSWORD` from it, so there is nothing to keep in sync.
2. Flash firmware and the dashboard filesystem:
   ```sh
   pio run -e esp32-serial -t upload
   pio run -e esp32-serial -t uploadfs
   pio device monitor
   ```
3. If no Wi-Fi is configured, join `GaggiaSavant-Setup-XXXXXX` (password `AP_PASSWORD`),
   browse to `http://192.168.4.1`, open **Wi-Fi**, pick a network, Save. The device reboots
   onto your network.

## Unit tests (PC, no hardware)

```sh
pio test -e native
```
Runs on the PC with the host compiler (g++ or clang). Covers the control logic and every
software safety fault, sensor decoding, settings validation and the PID. See
[test/README.md](test/README.md). Run it after changing anything in `machine_logic`,
`sensor_math`, `settings_schema` or `pid.h`.

## Updating over Wi-Fi

```sh
pio run -e esp32-ota -t upload      # firmware
pio run -e esp32-ota -t uploadfs    # dashboard files
```
If `gaggia-savant.local` doesn't resolve, set `upload_port` in `platformio.ini` to the IP.
The OTA password comes from `OTA_PASSWORD` in `include/secrets.h`; the environment variable
`GAGGIA_OTA_PASS` overrides it (e.g. when the device still has an older password).

If a filesystem upload fails part-way, the device mounts LittleFS again. If the partition was
already partly overwritten, `/` serves the built-in Wi-Fi setup page instead of the dashboard;
use its **Firmware update** link (or `uploadfs`) to upload `littlefs.bin` again.

Or from the browser: **Update** tab → choose `.pio/build/esp32-serial/firmware.bin`
(or `littlefs.bin` from `pio run -t buildfs`) → Upload. Login is `ADMIN_USER` / `ADMIN_PASSWORD`.

Bump `-DFW_VERSION` in `platformio.ini` to see the new version on the dashboard.

## HTTP API

| Method | Path                           | Auth*  | Description                          |
|--------|--------------------------------|--------|--------------------------------------|
| GET    | `/api/status`                  | –      | Device status JSON                   |
| WS     | `/ws`                          | –      | Status JSON pushed every 2 s         |
| GET    | `/api/machine`                 | –      | Machine state, temperature, pressure, outputs, fault |
| WS     | `/ws` (`"type":"machine"`)     | –      | Machine JSON pushed every 0.5 s      |
| GET    | `/api/settings`                | –      | Settings values, defaults and limits |
| POST   | `/api/settings` (any of the setting keys) | admin | Validate, apply and save settings |
| POST   | `/api/settings/reset`          | admin  | Restore default settings             |
| GET    | `/api/io`                      | –      | Pin names/types (from io_pins.h) + live state |
| GET    | `/api/wifi/scan`               | –      | Nearby networks (async; poll)        |
| POST   | `/api/wifi` (`ssid`, `pass`)   | admin† | Save credentials and reboot          |
| POST   | `/api/wifi/forget`             | admin  | Clear credentials and reboot         |
| POST   | `/api/reboot`                  | admin  | Reboot                               |
| POST   | `/api/update?target=firmware\|fs` | admin | Multipart `.bin` upload (one file), reboots on success; `409` while another update (browser or ArduinoOTA) is running |
| GET    | `/update`                      | admin  | Minimal fallback upload page         |

\* HTTP Basic auth. † Open while in setup-AP mode so first-time provisioning works.

Every POST must also carry the header `X-Savant: 1`, or an `Origin` header equal to the
device's own address; anything else gets `403`. This stops other web pages open in the
same browser from changing settings (cross-site request forgery). The dashboard pages add the
header automatically. The address used must also be one of the device's own names: its IP
address, `gaggia-savant`, or `gaggia-savant` followed by `.local`, `.lan`, `.home`, `.home.arpa`,
`.localdomain` or `.internal`. This blocks DNS rebinding, where another website's name is
pointed at the machine. POSTs sent to any other name, including through a reverse proxy, get
`403`. From a script:

```sh
curl -u admin:PASSWORD -H 'X-Savant: 1' -d coffeeTempC=106 http://gaggia-savant.local/api/settings
```

The server sends no CORS headers, so other sites can't read the API either.

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
  pull-up/down and level (HIGH/LOW), refreshed every second. Reading is non-invasive and the
  page never touches the ADC: the pressure pin shows the control task's latest sample.
  ADC2 pins can't be read while Wi-Fi is on — use ADC1 (GPIO32–39) for analog inputs.

## Machine operation

| Coffee | Steam | State       | Boiler target | Pump                 | Valve  |
|--------|-------|-------------|---------------|----------------------|--------|
| off    | off   | `IDLE`      | coffee (107 °C) | off                | closed |
| on     | off   | `BREWING`   | coffee        | PID to brew pressure (6 bar) | open |
| off    | on    | `STEAM`     | steam (145 °C)  | off                | closed |
| on     | on    | `HOT_WATER` | steam         | 100 %                | closed |

- **Lights:** the active mode's light is on when the boiler is within ±2 °C of its target.
  Both blink on a fault.
- **Coffee switch arming:** if the Coffee switch is on at power-up (or after an update),
  it is ignored until turned off and on again. The pump also stops after 120 s of
  continuous running and needs the same off/on cycle.
- **Faults** (latched until power cycle, everything off): thermocouple fault, boiler
  above 155 °C, boiler not heating at full power for 120 s (high-limit tripped, fuse,
  element or loose probe), no zero-cross signal while pumping, pressure sensor out of
  range while brewing. The dashboard shows the cause.
- Firmware updates and reboots switch all loads off first.
- Settings (temperatures, pressure, PID gains) are on the dashboard **Settings** page.

## Adding your own features

- Machine behaviour lives in `src/machine.cpp` (`tick()` runs every 50 ms).
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
