# Test plan

## Simulator tests

- All screens fit 320 x 240.
- No critical text is below 12 px.
- Main rows are at least 34 px tall.
- Bottom buttons are at least 44 px tall.
- Navigation works with touch and keyboard.
- Fake events update UI without blocking.

## Firmware bring-up tests

- Serial boot log appears.
- Display initializes.
- LVGL tick runs.
- Touch taps register.
- Keyboard input registers.
- Trackball focus moves.
- Battery voltage reads.
- SD card mounts.
- GPS NMEA data reads.
- Radio initializes.

## Hardware-in-the-loop tests

See `docs/HARDWARE_IN_LOOP_AUTOMATION.md` for the real-device automation plan.
The short version is: build only `tdeck-plus-915`, flash a fixed T-Deck Plus on
`dev-host.local`, drive UI/mesh scenarios through a serial test shell, collect
logs and framebuffer CRCs, and publish the report through the web UI.

## Mesh tests

- Two-device direct send.
- Two-device direct receive.
- Reboot preserves device name and region.
- Node list updates after packets.
- RSSI/SNR display updates.
- UI remains responsive during TX/RX.

## Field tests

- Indoor short range.
- Outdoor line-of-sight.
- Low battery behavior.
- Sleep and wake.
- SD card removed.
- GPS no-fix state.
