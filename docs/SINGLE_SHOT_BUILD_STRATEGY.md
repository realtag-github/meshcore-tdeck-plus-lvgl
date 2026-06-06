# Single-shot build and simulation strategy

## Goal

Build once around a shared app core so the same UI and state model can run in:

- an LVGL desktop simulator
- the ESP32-S3 firmware on T-Deck Plus

## What can be simulated well

The simulator can validate:

- 320 x 240 layout
- font size and readability
- touch target size
- navigation flow
- message list behavior
- settings screens
- fake nodes and fake RSSI/SNR
- fake GPS data
- fake battery status
- app event routing

## What cannot be simulated fully

The simulator cannot accurately validate:

- SX1262 radio timing
- LoRa packet loss
- MeshCore routing under real RF conditions
- GPS serial timing
- battery readings
- power draw
- sleep/wake behavior
- keyboard/trackball electrical behavior
- SD card edge cases on the board

## Single-shot architecture

Use compile-time targets:

```text
APP_TARGET_SIMULATOR
APP_TARGET_TDECK_PLUS
```

Shared code:

```text
app state
message model
event bus
screen definitions
LVGL widgets
navigation
```

Target-specific code:

```text
display driver
touch driver
keyboard driver
trackball driver
radio driver
GPS driver
battery driver
SD driver
```

## Recommended execution order

1. Create the development host Docker build container with SSH access.
2. Install the required simulator and firmware toolchains in that container.
3. Create LVGL simulator app at exact 320 x 240.
4. Implement all screens with fake app data.
5. Run readability review.
6. Add input navigation and focus states.
7. Compile ESP32 firmware with stub MeshService.
8. Bring up display, touch, keyboard, and battery.
9. Replace MeshService stub with MeshCore wrapper.
10. Test two-device direct message.
11. Add persistence and diagnostics.
12. Package first alpha release.

## development host Docker build container

Create the development container on `dev-host.local` before firmware or simulator
work. It must include:

- SSH server with the configured development user
- C++17 compiler
- CMake
- Ninja
- SDL2 development headers
- Python 3
- PlatformIO
- Git

Use:

```bash
cd meshcore_tdeck_plus_lvgl_plan
tools/run_dev-host_docker.sh
```

The default published SSH endpoint is:

```text
dev-host.local:2231
```

The default simulator snapshot endpoint is:

```text
http://dev-host.local:8092/screens.html
```

The default interactive LVGL noVNC endpoint is:

```text
http://dev-host.local:8094/vnc.html?autoconnect=true&resize=scale
```

## Build approach

Use CMake for the simulator and PlatformIO for the ESP32 firmware. Keep shared code in a location both can include.

A future refactor can move common code into:

```text
common/app
common/ui
common/model
```

For the first implementation, keep it simple and duplicate only the minimum build glue.
