# Execution status

Last updated: 2026-05-09

## Completed in Docker on development host

- Created `meshcore-tdeck-plus-dev` Docker image.
- Started `meshcore-tdeck-plus-dev` container on `dev-host.local`.
- Enabled SSH access on port `2231` for the configured development user.
- Installed build requirements:
  - g++ 13.3
  - CMake 3.28
  - SDL2 development headers
  - Python 3
  - PlatformIO 6.1.19
- Built the fallback simulator with `make run`.
- Built the simulator with CMake.
- Generated `simulator/build/screens.html` with 12 screens.
- Served the simulator snapshot at:

```text
http://dev-host.local:8092/screens.html
```

- The static mock web UI exposes Launcher image downloads at:

```text
http://dev-host.local:8092/downloads/MeshCore-TDeckPlus-Launcher-915.bin
http://dev-host.local:8092/downloads/MeshCore-TDeckPlus-Launcher-868.bin
http://dev-host.local:8092/downloads/MeshCore-TDeckPlus-Launcher-433.bin
```

- Built and started the real LVGL SDL2 simulator.
- Exposed the interactive LVGL simulator through noVNC at:

```text
http://dev-host.local:8094/vnc.html?autoconnect=true&resize=scale
```

- Added and passed the simulator navigation test that verifies every screen has
  a touch action that exits the screen and every non-Home screen has a touch path
  back to Home.
- Compiled hardware-target firmware for:
  - `tdeck-plus-915`
  - `tdeck-plus-868`
  - `tdeck-plus-433`

## Implemented app scope

- Shared app state model.
- Shared event bus.
- Shared mock MeshCore data for the desktop simulator.
- Shared navigation table used by simulator, firmware, and navigation tests.
- Shared app controller for stateful UI commands.
- Firmware MeshService command bridge for UI-initiated sends, settings changes,
  room/repeater commands, real radio RX traffic, status refresh, and persisted
  radio/UI settings.
- Firmware BLE companion bridge based on the upstream MeshCore companion node
  protocol:
  - Nordic UART service UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`.
  - RX/TX characteristic UUIDs matching upstream MeshCore.
  - Static companion PIN `123456` exposed in device info.
  - App start, device query, battery/storage, channel get/set, direct contact
    text send, channel text send, message sync, and messages-waiting push frames.
  - Radio parameter updates for frequency, bandwidth, spreading factor, coding
    rate, client-repeat flag, TX power, and path-hash mode.
  - Device time get/set, advert name/location settings, explicit self-advert
    send, tuning params get/set, and other-params settings.
  - Core/radio/packet stats responses, auto-add config get/set, and allowed
    repeat-frequency responses.
  - Private-key export/import backed by the persisted MeshCore Ed25519 identity.
  - Contact list, add/update contact, get contact by public key, remove contact,
    and reset contact path frames.
  - Channel names and 16-byte secrets are persisted with the other UI settings.
  - Channel text sent from BLE is routed through the firmware compose/radio path
    as broadcast traffic and added to message history.
- Upstream MeshCore is now tracked as `third_party/MeshCore` via git submodule.
- Firmware builds and links upstream MeshCore as a PlatformIO dependency.
- Firmware has a `MeshCoreCoreFacade` boundary so LVGL/service code can migrate
  onto upstream MeshCore without coupling screens directly to upstream headers.
- Firmware now creates a MeshCore-compatible Ed25519 node identity on first boot,
  persists it in ESP32 Preferences, and exposes the real public key through the
  facade instead of using a placeholder key.
- The local `firmware/lib/meshcore_ed25519` wrapper compiles MeshCore's vendored
  ed25519 C implementation without modifying the upstream submodule.
- Broadcast/channel radio sends now use upstream MeshCore encrypted group text
  packet framing (`PAYLOAD_TYPE_GRP_TXT`) with the selected channel secret.
- Received MeshCore group text packets are decrypted against the configured
  channels and routed into the same UI/message-history path.
- Node/contact records now persist MeshCore public keys, route paths, contact
  type, contact flags, and last-modified metadata.
- Direct radio sends to contacts with public keys now use upstream MeshCore
  encrypted direct text datagrams (`PAYLOAD_TYPE_TXT_MSG`); contacts without
  public keys still use the transitional raw `MC1|...` bridge.
- Received MeshCore direct text datagrams from known contacts are decrypted and
  routed into the same UI/message-history path.
- Received MeshCore direct text datagrams now emit upstream-style ACK/path-return
  packets to improve compatibility with stock MeshCore delivery tracking and
  return-path discovery.
- Received MeshCore path-return packets update each contact's persisted route
  path so later direct sends can use routed delivery where possible.
- The firmware periodically emits signed upstream MeshCore chat advertisements,
  and valid received advertisements are imported into persisted contacts with
  public key, name, type, RSSI/SNR, and optional position.
- Legacy plain `MC1|...` direct and `POS1|...` position frames remain accepted
  during the migration.
- BLE self-info now advertises the persisted MeshCore public key instead of a
  padded local node ID.
- Hardware service layer for battery, GPS, storage, input, and radio status.
- Official LILYGO T-Deck pin map applied for power, SPI, display, LoRa, battery,
  touch interrupt, keyboard interrupt, SD, GPS, I2S, and trackball direction
  pins.
- Board power and display backlight pins are enabled during service startup.
- Firmware LVGL now has a T-Deck display/touch port:
  - LovyanGFX ST7789 display backend configured like Meshtastic's T-Deck TFT
    path: SPI2 host, 3-wire SPI, shared bus, inverted 240x320 panel, landscape
    rotation.
  - LVGL v9 display buffer registration.
  - direct GT911 I2C touch reader with coordinate transform.
- LVGL flushes now send RGB565 frames through LovyanGFX instead of the plain
  upstream TFT_eSPI ST7789 path, matching Meshtastic's current T-Deck approach.
- Firmware now has an explicit `lv_conf.h`; the LVGL missing-config warning is
  gone and LVGL heap is set to 64 KiB.
- Physical keyboard support reads the LILYGO keyboard controller at I2C `0x55`;
  printable keys edit compose text, Backspace deletes, and Enter queues send.
- Trackball direction GPIOs update selected message/node/channel or map zoom
  according to the active screen.
- GPS service now reads NMEA sentences from `Serial1` on the T-Deck Plus GPS pins
  and parses GGA/RMC latitude/longitude when a fix is available.
- GPS is disabled by default on first boot; users can enable it from the Settings
  screen or serial `gps` command, and the preference is persisted after changes.
- SD service mounts the card through Arduino `SD`/`SPI` and appends field logs to
  `/meshcore.log` when writable.
- Message history is persisted to `/meshcore_messages.log` on the SD card for
  UI sends, keyboard sends, serial sends, and real radio RX packets.
- Message deletes append tombstone records so deleted messages do not reappear
  after reboot when history is restored.
- The serial console can clear persisted message history with `clear-messages`.
- Node/contact history is persisted to `/meshcore_nodes.log`, restored at boot,
  and updated from real radio RX sender IDs.
- Radio service now initializes the SX1262 through RadioLib, applies regional
  frequency/TX-power changes, applies BLE radio-parameter writes, transmits
  direct frames, and polls for received bytes that flow into the inbox.
- Radio receive is now polled independently from slower battery/GPS/storage
  refreshes, so LoRa receive checks run about every 25 ms instead of once per
  second.
- Radio direct frames now use a simple structured `MC1|sender|target|body`
  format, while received legacy `sender:body` and raw payloads are still accepted.
- Structured direct RX frames are ignored when the target is another node;
  `*`, `all`, `broadcast`, or an empty target are treated as broadcast.
- Position frames use `POS1|sender|target|lat|lon`, update node positions, and
  can be transmitted from the serial console with `send-pos <node>`.
- Regional firmware builds default to their matching region before stored
  preferences are applied.
- Local device name and node ID are persisted; the default node ID is derived
  from the ESP32 MAC suffix and is used as the sender in structured radio
  frames.
- `tools/flash_tdeck.sh` and `tools/monitor_tdeck.sh` provide hardware bring-up
  commands for upload and serial monitoring.
- `tools/package_firmware.sh` creates `dist/` regional firmware binaries and a
  SHA-256 manifest.
- `tools/package_firmware.sh` also emits Launcher-ready app-only binaries named
  `MeshCore-TDeckPlus-Launcher-<region>.bin` for bmorcelli Launcher SD/WebUI/OTA
  installs.
- USB serial diagnostics console:
  - `help`
  - `status`
  - `logs`
  - `messages`
  - `nodes`
  - `clear-messages`
  - `clear-nodes`
  - `name <text>`
  - `node <id>`
  - `rename-node <id> <name>`
  - `region`
  - `power <dbm>`
  - `path`
  - `gps`
  - `audio`
  - `room`
  - `admin`
  - `send <node> <text>`
  - `send-pos <node>`
- USB serial `status` now includes BLE advertising/connection state.
- USB serial `status` now includes the MeshCore public-key prefix, and
  `identity` prints the full persisted MeshCore public key.
- Shared LVGL UI module used by both simulator and firmware.
- Boot, Home, Inbox, Message, Compose, Nodes, Channels, Map, Settings, Radio,
  Servers, and Diagnostics interactive LVGL screens.
- Home is now the startup/master screen and renders as a Windows CE 5.0-style
  desktop with screen icons and a Start taskbar.
- Non-desktop screens render as maximized Windows CE-style windows ending above
  the fixed bottom taskbar; their touch actions are taskbar buttons so content
  does not overlap the taskbar.
- Non-desktop screens have a red close `X` in the title bar, and battery plus
  frequency are shown only in the bottom taskbar tray.
- The LVGL simulator and firmware now use compact 10/12 px fonts so the noVNC
  output aligns with the static `screens.html` mock scale.
- The static `screens.html` renderer now uses the same Windows CE-style shell,
  gray workspace, white list rows, compact taskbar buttons, red close button,
  and taskbar tray as the live LVGL/noVNC view.
- Desktop icons no longer use one-letter placeholders; LVGL/noVNC uses bundled
  Font Awesome symbols and `screens.html` uses matching line pictograms.
- Incoming messages now show a topmost Windows CE-style notification overlay
  above any active screen for 5 seconds, then disappear automatically.
- LVGL screen transitions auto-delete the previous screen so repeated simulator
  service refreshes do not accumulate stale screens.
- The noVNC simulator polls pointer input at a short interval and includes a
  simulator touch router for desktop icons, the taskbar Start button, window
  close, and taskbar actions.
- Diagnostics now exposes runtime uptime, heap, PSRAM, battery millivolts, SD
  write state, persisted-message count, and persisted-node count.
- Simulator-only direct-message send/delete/reply, node scan/ping/selection,
  channel join/leave/selection, map zoom/center, region/TX-power/path-hash
  controls, GPS toggle, room-server login, repeater-admin toggle, clock sync,
  and diagnostics log refresh.
- Background firmware service updates now flow back into the currently displayed
  LVGL screen through a versioned app snapshot.
- The desktop LVGL simulator now runs a simulator service loop that injects
  background packets into the same shared state path.
- T-Deck firmware boots from a clean hardware-backed app snapshot, does not use
  desktop mock state, does not synthesize incoming traffic, and reports radio
  send failure instead of accepting a fake transmit when hardware is unavailable.
- `tools/verify_all.sh` runs the static simulator, LVGL simulator build,
  navigation test, app-controller test, and all three PlatformIO firmware builds.
- Build optimization:
  - Docker image installs ccache.
  - development host run script mounts persistent PlatformIO, ccache, pip, and CMake cache
    directories under `${HOME}/.cache/meshcore-tdeck-plus`.
  - Firmware builds run all three regional environments in parallel through
    `tools/verify_firmware_parallel.sh`.
  - `tools/verify_fast.sh` runs simulator-only checks.
  - `tools/verify_all.sh` composes fast checks plus parallel firmware builds.
  - `tools/package_firmware.sh` now emits firmware, bootloader, partitions, and
    merged ESP32-S3 images with SHA-256 checksums.
  - `tools/package_firmware.sh` emits explicit bmorcelli Launcher-compatible
    app images and marks merged images as direct USB/esptool recovery artifacts.
- Firmware shell using LVGL and the current hardware-backed MeshService.

## Hardware-only blockers

These steps require physical hardware and cannot be completed inside Docker:

- Display, touch, keyboard, trackball, battery, SD, GPS, and LoRa pin validation.
- Real T-Deck Plus display bring-up.
- Real MeshCore radio wrapper validation.
- Real BLE pairing and MeshCore companion app interoperability validation.
- Two-device direct message test.
- Field test for RSSI/SNR, GPS, battery, and persistence behavior.

## Next execution step on hardware

Flash `firmware/.pio/build/tdeck-plus-915/firmware.bin` or the matching regional
build to a T-Deck Plus, then replace `board_pins.h` placeholders with verified
board-revision pin mappings.
