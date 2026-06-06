# Project plan

## Product goal

Create an open-source MeshCore firmware for T-Deck Plus with a readable LVGL UI, optimized for the 320 x 240 screen and physical keyboard.

## MVP

The first alpha should include:

- boot screen
- home/status screen
- inbox
- message view
- compose screen
- nodes list
- channels list
- settings list
- diagnostics screen
- battery indicator
- MeshCore direct send/receive
- region preset selection
- serial logs

## Architecture

```text
LVGL screens
    |
App event bus
    |
App state and stores
    |
Services
    |-- MeshService
    |-- GpsService
    |-- BatteryService
    |-- StorageService
    |-- InputService
    |
Hardware drivers or simulator mocks
```

## Phases

### Phase 1: UI simulator

- Build exact 320 x 240 simulator.
- Implement static screens using fake data.
- Verify readability.
- Verify touch target sizes.

### Phase 2: Firmware shell

- Boot ESP32-S3.
- Initialize display.
- Initialize LVGL.
- Render home screen.
- Add touch and keyboard input.

### Phase 3: Hardware drivers

- Battery ADC
- SD card
- GPS serial
- Keyboard matrix/input
- Trackball/encoder
- Speaker beep

### Phase 4: MeshCore integration

- Wrap MeshCore behind MeshService.
- Add direct messages.
- Add node discovery.
- Show RSSI/SNR.
- Persist settings.

### Phase 5: Alpha release

- Compile 433/868/915 MHz builds.
- Publish merged binaries.
- Publish flashing guide.
- Publish known issues.

## Non-goals for v0.1

- offline maps
- audio messages
- file transfer
- OTA updates
- complex routing UI
- encrypted key-management UI
