# Washe Huinya

![alt text](image.png)
WasheHuinya is firmware and a web interface for the ESP32-S3 HID MacroPad. The device can send keyboard and mouse HID events over USB or Bluetooth LE, while being controlled via Wi-Fi through the built-in web UI, HTTP API, TTY commands, and physical GPIO buttons.

The project is designed for scenarios where you need to quickly launch repetitive actions: typing text, keyboard shortcuts, mouse movement and clicks, scrolling, loops, macro profiles, and remote firmware updates.

## Features

* ESP32-S3 firmware based on the Arduino framework via PlatformIO.
* USB HID mode: `Keyboard + mouse`.
* Bluetooth LE HID modes: `Keyboard` and `Mouse`.
* Built-in Wi-Fi access point for initial setup and fallback access.
* Connection to an external Wi-Fi network in STA mode.
* Built-in web UI served from LittleFS with the following tabs: `Control`, `TTY`, `GPIO`, `Wi-Fi`, `HID / BLE`, `Settings`, `Observe`, and `Update`.
* Text macros with keyboard and mouse commands, delays, repetitions, and infinite loops.
* Saving, running, importing, and exporting macro profiles.
* Launching saved profiles from GPIO buttons.
* System GPIO `STOP` command, assigned to `GPIO4` by default.
* TTY commands over HTTP and a TTY log stream via WebSocket.
* HTTP API documented with OpenAPI.
* OTA updates for firmware and the LittleFS image through the web UI.
* Observability: firmware version, uptime, heap, flash, LittleFS, Wi-Fi, BLE, API counters, reconnects, and macro execution counters.
* RGB status indication via WS2812 on `GPIO21`.

## Hardware Platform

The current build environment is defined in `platformio.ini`:

```ini
default_envs = esp32s3zero
```

Main parameters:

* PlatformIO board: `esp32-s3-devkitc-1`;
* Flash: `4MB`;
* Filesystem: `LittleFS`;
* Partitions: `partitions.csv`;
* Firmware version: `0.5.1`;
* Monitor speed: `115200`;
* Upload speed: `921600`;
* Default AP SSID: `HIDPad-Setup`;
* Default AP password: `hidpad1234`;
* Dependency: `ArduinoJson 7.4.x`.

Flash layout:

```text
nvs       0x9000    0x5000
otadata   0xe000    0x2000
app0      0x10000   0x180000
app1      0x190000  0x180000
littlefs  0x310000  0x0F0000
```

## Quick Start

1. Connect the ESP32-S3 via USB for power and firmware flashing.

2. Build and upload the firmware.

3. Build and upload the LittleFS image from the `data` directory.

4. Connect to the device's Wi-Fi access point:

   ```text
   SSID: HIDPad-Setup
   Password: hidpad1234
   ```

5. Open the web UI:

   ```text
   http://192.168.4.1
   ```

6. On the `Wi-Fi` tab, enter the SSID and password of your network.

7. After the device connects to the router, open it using the mDNS hostname or the IP address assigned by the router:

   ```text
   http://hidpad-s3.local
   ```

8. On the `HID / BLE` tab, select the transport: `USB` or `Bluetooth LE`.

9. On the `Control` tab, select an example profile or create your own macro.

10. Click `Test` to run the text from the editor without saving it, or click `Save` and `Run saved` to execute a saved profile.

mDNS is not available on all networks and operating systems. If `hidpad-s3.local` does not open, use the IP address shown in the web UI or in the router's DHCP client list.

## Build and Flash

Run these commands from the repository root.

Build the firmware:

```bash
pio run -e esp32s3zero
```

Upload the firmware:

```bash
pio run -e esp32s3zero -t upload
```

Build the LittleFS image:

```bash
pio run -e esp32s3zero -t buildfs
```

Upload the LittleFS image:

```bash
pio run -e esp32s3zero -t uploadfs
```

Open the serial monitor:

```bash
pio device monitor -b 115200
```

After changing C++ code, the firmware must be uploaded again. After changing files in `data`, the LittleFS image must be uploaded again.

## Project Structure

```text
include/                 Firmware service header files
src/                     ESP32-S3 application C++ code
data/                    Web UI and default profiles for LittleFS
data/index.html          Main web UI page
data/script/             Web UI JavaScript
data/style/              Web UI CSS
data/profiles/           Example macro profiles
docs/WasheHuinya.png     Project image for the README
docs/software/           Detailed device documentation
docs/software/openapi.yaml
                         OpenAPI HTTP API specification
partitions.csv           Flash partition layout
platformio.ini           PlatformIO build configuration
```

Key firmware modules:

* `Application` — application initialization and main loop.
* `WifiService` — AP/STA Wi-Fi, hostname, and captive portal.
* `WebApiServer` — web UI, HTTP API, OTA updates, and observability.
* `StorageService` — settings, profiles, and GPIO mappings stored in non-volatile storage and LittleFS.
* `MacroEngine` — macro command parsing and execution.
* `CommandService` — profile execution, TTY commands, and runtime commands.
* `BleHidService` — Bluetooth LE HID keyboard/mouse.
* `GpioService` — physical buttons and the `STOP` command.
* `LedService` — WS2812 RGB status indication.
* `UpdateService` — firmware and filesystem updates.
* `RuntimeTasks` — periodic runtime tasks.

## Web UI

The web UI is stored in `data` and uploaded to the device as a LittleFS image.

Main tabs:

* `Control` — macro editor, profiles, execution, testing, import, and export.
* `TTY` — send TTY commands and view the TTY log in real time.
* `GPIO` — assign profiles or the `STOP` command to physical buttons.
* `Wi-Fi` — configure the STA network and reset saved Wi-Fi settings.
* `HID / BLE` — select USB/BLE transport, BLE role, and manage bonds.
* `Settings` — hostname, default profile, and TTY password.
* `Observe` — firmware, memory, filesystem, Wi-Fi, BLE, and runtime status/counters.
* `Update` — upload a new `firmware.bin` and `littlefs.bin` through the browser.

## Macros

A macro profile is a text-based script. Examples are stored in `data/profiles` and uploaded to the device together with the LittleFS image.

Supported commands:

```text
TEXT <text>
TYPE <text>
DELAY <ms>
WAIT <ms>
KEY <key or modifiers>
HOTKEY <key or modifiers>
MOUSE MOVE <x> <y> [jitter]
MOUSE WHEEL <delta>
MOUSE SCROLL <delta>
MOUSE PAN <delta>
MOUSE HWHEEL <delta>
MOUSE CLICK <button>
MOUSE PRESS <button>
MOUSE RELEASE <button>
RELEASEALL
REPEAT <count>
LOOP
END
```

Macro comments:

```text
# comment
; comment
// comment
```

Example keyboard macro:

```text
KEY CTRL ALT T
DELAY 500
TEXT echo HID MacroPad ready
KEY ENTER
```

Example repeated mouse movement:

```text
REPEAT 4
  MOUSE MOVE 120 0 3
  DELAY 120
  MOUSE MOVE 0 120 3
  DELAY 120
  MOUSE MOVE -120 0 3
  DELAY 120
  MOUSE MOVE 0 -120 3
  DELAY 120
END
```

`LOOP ... END` executes a block indefinitely until the `Stop` button, the `STOP` TTY command, or the GPIO `STOP` command is triggered.

Profile names are validated: only Latin letters, digits, `_`, and `-` are allowed. Spaces are not allowed.

## GPIO Buttons

A button is connected between the selected GPIO pin and `GND`. The firmware uses `INPUT_PULLUP`, interrupts, and debounce handling.

The following pins are available in the UI:

```text
GPIO4, GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10
```

Behavior:

* press a button: the assigned profile starts;
* hold a button: the profile repeats;
* release the button: the current macro iteration continues until it finishes;
* press another button while holding the first one: the new profile takes priority and the previous one is stopped;
* the `STOP` command is always available and is assigned to `GPIO4` by default.

Wiring details are described in `docs/software/GPIO.md`.

## USB and Bluetooth LE HID

In USB mode, the device operates as an HID MacroPad with both keyboard and mouse commands available simultaneously.

In BLE mode, only one role is available at a time:

* `Keyboard` — `TEXT`, `TYPE`, `KEY`, `HOTKEY`, and the keyboard part of `RELEASEALL`;
* `Mouse` — `MOUSE ...` commands and the mouse part of `RELEASEALL`.

BLE device names depend on the selected role:

```text
HIDPad S3 keyboard
HIDPad S3 mouse
```

When changing the role, it is recommended to remove the old pairing from the computer or phone and pair the device again, because the operating system may cache the device name and HID descriptor.

## TTY

In this project, TTY is a network command interface over Wi-Fi, not necessarily a Linux `/dev/tty*` device.

Basic commands:

```text
HELP
PING
RUN <profile>
STOP
LINE <macro_line>
```

Examples:

```text
RUN hello
RUN mouse_test
STOP
LINE KEY CTRL ALT T
LINE TEXT git status
LINE KEY ENTER
```

Details about TTY authentication, HTTP requests, and the WebSocket log are described in `docs/software/TTY.md`.

## HTTP API

The OpenAPI specification is located at `docs/software/openapi.yaml`.

Main endpoints:

```text
GET  /api/ping
GET  /api/status
GET  /api/observability
GET  /api/profile/load?name=...
POST /api/profile/save
GET  /api/profile/export
POST /api/profile/import
POST /api/profile/delete
POST /api/run
POST /api/run/script
POST /api/stop
POST /api/wifi/save
POST /api/wifi/reset
POST /api/ble/save
POST /api/ble/bond/delete
POST /api/ble/bonds/clear
POST /api/gpio/save
POST /api/gpio/delete
POST /api/tty/exec
GET  /api/tty/log
POST /api/update/firmware
POST /api/update/filesystem
GET  /api/update/progress
```

Successful responses are returned as JSON. API errors use a consistent format:

```json
{
  "error": {
    "code": "profile_not_found",
    "message": "Profile was not found"
  }
}
```

## Web UI Updates

The `Update` tab accepts:

* `firmware.bin` — firmware image;
* `littlefs.bin` — filesystem image.

When updating both files, the web UI uploads the firmware first and then the LittleFS image. After a successful upload, the device reboots.

Details are described in `docs/software/FIRMWARE_UPDATE.md`.

## Documentation

* `docs/software/README.md` — detailed description of the web UI, macros, API, and device behavior.
* `docs/software/TTY.md` — TTY commands, authentication, and WebSocket logging.
* `docs/software/GPIO.md` — button wiring and profile assignment.
* `docs/software/LED.md` — RGB indicator states.
* `docs/software/FIRMWARE_UPDATE.md` — firmware and LittleFS updates through the web UI.
* `docs/software/BIN_FLASHING.md` — manual flashing using ready-made `.bin` files.
* `docs/software/openapi.yaml` — HTTP API specification.

## Security Notes

* The device sends real HID events to the connected host. Test macros using `Test` and run them only when the appropriate application/window is active.
* Default AP password: `hidpad1234`. It is intended for initial setup. After configuration, use the connection to your own Wi-Fi network.
* TTY authentication should not be disabled on a shared or untrusted network.
* BLE bonds must be removed separately from the ESP32-S3 and from the host. After deleting a bond in the web UI, also remove the device from the operating system's Bluetooth settings.
