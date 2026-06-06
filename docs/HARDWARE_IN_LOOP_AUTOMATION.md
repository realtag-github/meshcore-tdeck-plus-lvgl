# Hardware-In-The-Loop Automation

The standalone T-Deck firmware now exposes a serial HIL console, and the host has a one-command loop runner for the real device.

## Real Hardware Loop

Default WiFi OTA loop:

```bash
MESHCORE_TDECK_SERIAL=/dev/tdeck-plus \
MESHCORE_TDECK_HOST=<TDECK_IP> \
tools/dev_cycle.sh --upload wifi
```

USB flash loop:

```bash
tools/dev_cycle.sh --upload usb --serial /dev/tdeck-plus
```

Monitor/test already-flashed firmware without uploading:

```bash
tools/hil_tdeck_loop.py --upload none --no-build --serial /dev/tdeck-plus
```

The runner always builds/packages `tdeck-plus-915` only. It writes each run under:

```text
artifacts/hil/<timestamp>/
  build.log
  ota-upload.log or usb-upload.log
  serial.log
  summary.json
```

It also publishes the latest result for the web UI:

```text
simulator/build/hil-latest.json
simulator/build/hil-latest-serial.log
https://dev-host.local:8092/hil-latest.json
https://dev-host.local:8092/hil-latest-serial.log
```


## Container USB mapping

`tools/run_dev-host_docker.sh` maps the host T-Deck serial device into the container when it exists. Defaults:

```bash
HOST_TDECK_SERIAL=/dev/tdeck-plus
CONTAINER_TDECK_SERIAL=/dev/tdeck-plus
```

If the host only exposes `/dev/ttyACM0`, restart the container with:

```bash
HOST_TDECK_SERIAL=/dev/ttyACM0 tools/run_dev-host_docker.sh
```

## Firmware HIL Console

These commands are built into the standalone firmware over USB serial. They do not require BLE, touch, or the companion app.

```text
hil commands
hil ping
hil health
hil dump-state
ui screen
ui home
ui show <screen>
ui action <0-3>
ui scroll <delta>
ui key <text>
mesh inject-node <id> <name>
mesh inject-direct <sender> <text>
mesh inject-channel <channel> <sender> <text>
sys reboot
```

Useful screen names:

```text
Home Inbox Message Compose Nodes Contacts Channels ChannelEditor Map Settings Radio RadioAdvanced Identity Ble Servers Tools Diagnostics
```

## Pass Criteria

`tools/hil_tdeck_loop.py` fails the run if any of these happen:

```text
missing boot: setup complete
missing boot: MeshService started with hardware backend
Guru Meditation / panic / Backtrace / watchdog / assert / prohibited access
hil command timeout
expected serial response missing
OTA/USB upload failure
```

The default command script proves that the real flashed firmware can boot, keep its main loop alive, open core UI screens, inject inbound direct/channel messages, list persisted messages/channels, and report health.

Live MeshCore traffic used during development must go through `#test`. The firmware includes a `test` channel using the MeshCore hashtag key for `#test`, and the HIL script only injects local `test` channel traffic.

## Why This Is Standalone

The HIL console is inside `firmware/src/mesh/mesh_service.cpp`, not in the simulator. The same binary that runs on the real T-Deck accepts the automation commands. The host runner only orchestrates build, upload, serial logging, and assertions.

## Remaining Hardware Extensions

The current loop validates firmware behavior through serial-injected events. It does not physically press the touch panel or trackball. For full physical-input validation, add one of these later:

```text
USB HID/input jig driven by dev-host
small camera/screenshot fixture watching the screen
second MeshCore node as a radio peer for real LoRa RX/TX assertions
BLE client test using bleak against the companion service
```

## WebUI Hardware Bridge

The development web UI exposes a `Hardware` desktop icon. It talks to the attached T-Deck through the local bridge at `https://dev-host.local:8093/api/status`.

Buttons in that window map to the real device:

- `Status`: reads serial HIL health, WiFi state, firmware hash, and device path.
- `WiFi`: sends `wifi start` over serial and waits for the OTA server.
- `Deploy`: uploads `dist/tdeck-plus-915-firmware.bin` to the real T-Deck over WiFi OTA.
- `HIL Test`: starts WiFi, OTA deploys the current 915 MHz app image, reboots, and runs the serial HIL smoke test.
- `Build`: rebuilds and packages only `tdeck-plus-915`.
- `Cycle`: rebuilds, packages, starts WiFi, OTA deploys, and runs HIL.

The dev container exposes:

- Web UI: `https://dev-host.local:8092/`
- Hardware bridge: `https://dev-host.local:8093/api/status`
- noVNC fallback: `http://dev-host.local:8094/vnc.html?autoconnect=true&resize=scale`

The bridge is a development-only control surface. It assumes `/dev/tdeck-plus` is mapped into the container and writable.

## Real Screen Camera View

The `T-Deck Live Hardware` window on `https://dev-host.local:8092/` uses the host webcam pointed at the physical T-Deck. The host capture loop writes `simulator/build/camera.jpg`, and the web UI refreshes `https://dev-host.local:8092/camera.jpg` inside the 320x240 T-Deck window.

The same window polls `https://dev-host.local:8093/api/snapshot` for real firmware state and maps clicks back to serial HIL commands. On the desktop it maps icon regions to `ui show <screen>`. In app windows it maps the red close area to `ui home`, toolbar button regions to `ui action <index>`, and the mouse wheel to `ui scroll <delta>`.

If the image is upside down, click `Rotate` in the T-Deck Live Hardware toolbar. The setting is stored in browser local storage.

Host camera process:

```text
tools/start_host_camera_capture.sh
```

Runtime logs:

```text
/tmp/meshcore-tdeck-camera-supervisor.log
/tmp/meshcore-tdeck-camera.log
```
