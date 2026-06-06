# Upstream MeshCore Integration

The firmware is being moved toward this ownership model:

```text
third_party/MeshCore
  -> firmware/src/mesh/meshcore_core_facade.*
  -> firmware/src/mesh/mesh_service.*
  -> common/ui/app_ui.cpp
```

`third_party/MeshCore` is a git submodule that points at
`https://github.com/meshcore-dev/MeshCore`. Keep local T-Deck Plus UI and board
code outside that folder so upstream updates stay mechanical.

## Current State

- Upstream MeshCore is vendored as a submodule.
- Firmware builds and links upstream MeshCore through `firmware/platformio.ini`.
- MeshCore's vendored ed25519 C implementation is built by the local
  `firmware/lib/meshcore_ed25519` wrapper library so the submodule stays
  unmodified.
- `MeshCoreCoreFacade` is the new boundary that owns upstream-facing identity
  and radio parameter concepts.
- The facade creates a real MeshCore-compatible Ed25519 local identity on first
  boot, persists it in ESP32 Preferences, and exposes the public key through the
  firmware boundary.
- Broadcast/channel sends are encoded as upstream MeshCore `PAYLOAD_TYPE_GRP_TXT`
  packets using the configured channel secret, and received group text packets
  are decrypted back into the UI message stream.
- Contact records now carry MeshCore public keys, route paths, flags, and
  companion metadata in the persisted node history.
- Direct sends to contacts with public keys are encoded as upstream encrypted
  `PAYLOAD_TYPE_TXT_MSG` datagrams, and matching received direct text datagrams
  are decrypted back into the UI message stream.
- Received direct text datagrams trigger upstream-style ACK/path-return packets
  so stock MeshCore senders can learn a return path and mark receipt.
- Received upstream path-return packets update the persisted contact route path
  used by later direct sends.
- The firmware emits signed upstream MeshCore chat advertisements and imports
  valid received advertisements as contacts with public keys and optional
  position.
- The BLE companion bridge supports contact list, add/update, get-by-key,
  remove, path-reset, direct contact text send, and channel text send frames so
  the companion can provision real MeshCore contacts and route compose traffic.
- BLE radio settings now accept the upstream companion radio-parameter, TX-power,
  and path-hash commands and persist the actual numeric frequency/bandwidth/SF/CR
  values instead of deriving everything from a display label.
- BLE device-time, advert name/location, explicit self-advert, tuning, and
  other-params commands are now wired into the firmware settings/radio path.
- BLE stats, auto-add config, and allowed repeat-frequency queries now return
  upstream-shaped frames.
- BLE private-key export/import now uses the persisted upstream-compatible
  Ed25519 identity.
- Legacy plain `MC1|...` and `POS1|...` packets are still accepted for transition.
- Direct sends to contacts without public keys still use the raw-custom bridge
  until those contacts are imported or discovered as real MeshCore contacts.
- `MeshService` owns the facade and feeds it the current app snapshot.
- The existing LVGL UI remains unchanged and continues to talk only to shared
  app state and `MeshService`.

## Cutover Steps

1. Move radio transport from `RadioService` into an upstream `mesh::Radio`
   implementation for T-Deck Plus SX1262.
2. Replace the current BLE companion shim with upstream `MyMesh::handleCmdFrame`
   or a small adapter around it.
3. Promote upstream `ContactInfo` and path-return handling to the source of
   truth behind the current UI contact snapshot.
4. Replace message history with upstream offline queue/message events, mapping
   those events into UI snapshots.
5. Keep LVGL screens pure: they should never include upstream MeshCore headers.

## Update Workflow

```bash
git -C third_party/MeshCore fetch origin
git -C third_party/MeshCore checkout origin/main
PATH="$HOME/.local/bin:$PATH" \
  BUILD_DIR=/tmp/meshcore_tdeck_sim_build \
  LVGL_BUILD_DIR=/tmp/meshcore_tdeck_lvgl_build \
  PLATFORMIO_BUILD_DIR=/tmp/meshcore_pio_build \
  PLATFORMIO_LIBDEPS_DIR=/tmp/meshcore_pio_libdeps \
  ./tools/verify_all.sh
```

After an upstream bump, fixes should normally be limited to:

- `firmware/src/mesh/meshcore_core_facade.*`
- `firmware/lib/meshcore_ed25519` if upstream changes its vendored ed25519
  layout
- T-Deck Plus board/platform adapter files
- package/build metadata

Avoid patching files inside `third_party/MeshCore` unless the patch is intended
to be sent upstream.
