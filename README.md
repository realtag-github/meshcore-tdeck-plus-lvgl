# MeshCore LVGL Firmware for LILYGO T-Deck Plus

This package is a single-shot planning and starter scaffold for building an open-source MeshCore handheld firmware with an LVGL interface sized for the T-Deck Plus 320 x 240 display.

The design target is a rugged, retro embedded-terminal UI inspired by late-1990s/early-2000s handhelds, without using Microsoft, Windows, or other copyrighted system assets.

## What is included

- Full project plan and implementation roadmap
- 320 x 240 UI specification
- Screen-by-screen mock UI notes
- Firmware architecture
- PlatformIO starter scaffold
- LVGL screen skeletons
- Mesh service abstraction skeleton
- Stateful simulator-backed MeshCore UI commands for messages, nodes, channels,
  radio settings, room/repeater controls, map, GPS, and diagnostics
- Firmware service bridge that receives UI commands, persists key settings, and
  pushes simulated background MeshCore events back into LVGL
- Battery, GPS, storage, input, and radio service abstractions ready for real
  T-Deck Plus driver replacement
- Hardware LVGL port for ST7789 display, GT911 touch, physical keyboard, and
  trackball direction input
- USB serial diagnostics console for exercising status, region, GPS/audio,
  message injection, and simulated sends
- Desktop simulator strategy
- Test checklist
- Release checklist
- Last approved mockup image

## Important reality check

A true end-to-end firmware compile and hardware-equivalent simulation is not possible from a plan alone because MeshCore, board drivers, LoRa radio behavior, GPS, keyboard, trackball, SD, and power-management code must be wired to the exact upstream libraries and tested on real hardware.

The practical single-shot approach is:

1. Build the UI first in an LVGL desktop simulator at 320 x 240.
2. Keep the app model and event bus identical between simulator and firmware.
3. Replace simulator mocks with hardware drivers on the T-Deck Plus.
4. Compile firmware with PlatformIO.
5. Test radio behavior on at least two real devices.

## Recommended path

Start with `docs/SINGLE_SHOT_BUILD_STRATEGY.md`, then read `docs/UI_SPEC_320x240.md`, then implement from `firmware/`.

For a reproducible build host on `dev-host.local`, run:

```bash
tools/run_dev-host_docker.sh
```

Current execution status is tracked in `docs/EXECUTION_STATUS.md`.

To run the full verification suite inside the prepared build container:

```bash
tools/verify_all.sh
```

To build regional release binaries and checksums:

```bash
tools/package_firmware.sh
```

The package script also emits Launcher-ready app images:

```text
dist/MeshCore-TDeckPlus-Launcher-915.bin
dist/MeshCore-TDeckPlus-Launcher-868.bin
dist/MeshCore-TDeckPlus-Launcher-433.bin
```

Use those files from bmorcelli Launcher SD/WebUI/OTA installs. The
`*-merged.bin` files are full-flash images for direct USB/esptool recovery.

The interactive LVGL simulator runs in the development host container and is available at:

```text
http://dev-host.local:8094/vnc.html?autoconnect=true&resize=scale
```

The static mock web UI includes download buttons for those Launcher images:

```text
http://dev-host.local:8092/screens.html
```
