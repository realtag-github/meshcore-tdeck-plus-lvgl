# GUI gap analysis against MeshCore companion

This document compares the current T-Deck Plus LVGL GUI to the upstream
MeshCore companion command surface in
`third_party/MeshCore/examples/companion_radio/MyMesh.cpp`.

The firmware BLE bridge already implements part of that protocol in
`firmware/src/drivers/ble_companion.*` and wires many commands through
`firmware/src/mesh/mesh_service.cpp`. The gap here is the local on-device GUI:
what a user can do directly on the T-Deck Plus without the phone companion.

## Current GUI surface

The current desktop apps are:

- Chat: DMs, channels, message list, compose box, passive scrollbars.
- Contacts: selected contact details, public key prefix, path state,
  add/update metadata, reset path, share, and remove actions.
- Nodes: known node list, scan, ping, next node, map.
- Channel Editor: all channel slots, active state, user count, secret prefix,
  join/leave, next channel, and secret cycling.
- Map: simple local/selected-node position view and zoom controls.
- Settings: channel, node, GPS, Bluetooth, storage, and links to Identity/BLE.
- Radio: region preset, TX power, path hash mode, radio state.
- Radio Advanced: frequency, bandwidth, spreading factor, coding rate,
  client-repeat, tuning, manual contacts, auto-add, and packet counters.
- Identity: node name, public key prefix, advert policy, self advert,
  private-key export gate, import status, and device PIN state.
- BLE: enabled/connected state, RX/TX frame counters, last command, last error,
  and protocol link state.
- Servers: room server, repeater admin, clock sync, registration placeholders.
- Tools: status request, trace path, path discovery, telemetry, flood scope,
  custom vars, reboot/factory-reset placeholders.
- Diagnostics: radio, battery, memory, storage, Bluetooth, logs.

## Implemented high-priority GUI

These companion areas now have first-class on-device GUI coverage:

- Contact details and management:
  add/update contact, remove contact, reset path, get contact by key, show public
  key, route/path state, last-modified time, import/export/share contact.

- Channel editor:
  list all channel slots, edit channel name, show active channel, set channel
  secret, enable/disable channel, show user count, join/leave with persistence.

- Full radio editor:
  numeric frequency, bandwidth, spreading factor, coding rate, TX power,
  path-hash mode, region preset, and validation/error feedback.

- Advert/device identity:
  node name, public key, advert location policy, set advert lat/lon, send self
  advert now, last advert status.

- Message sync/history controls:
  explicit sync-next-message state, delivery/ACK state, delete message, reply
  from selected message, clear history, persisted/offline message count.

- BLE companion status:
  enabled/started/connected, RX/TX frame counters, last command, last error,
  connected companion state.

- GPS/location settings:
  GPS enabled, fix state, coordinates, satellites, advertise real GPS vs manual
  lat/lon vs no location.

## Remaining limitations

- Freeform ASCII text entry is available through the T-Deck keyboard/edit
  dialog for compose text, channel names/secrets, node name, PIN, custom vars,
  manual coordinates, and exact numeric radio fields. Remaining input polish is
  mostly around richer dialogs, selection/copy, and non-ASCII text.

- Some dangerous/admin actions are represented as armed/status actions instead
  of destructive execution: reboot, factory reset, private-key import/export,
  identity reset, and raw filesystem operations.

- Advanced datagrams still need specific workflows: binary request, anonymous
  request, raw data, channel data, control data, and signed-data flows.

## Low-priority or dangerous GUI guardrails

These should exist only behind an advanced/admin confirmation flow.

- Reboot.
- Factory reset.
- Private key import/export.
- Flood scope key and default flood scope.
- Custom variables.
- Filesystem/file operations.

## Companion commands without full GUI parity

From upstream `companion_radio/MyMesh.cpp`, these commands still lack full
interactive on-device parity. Some are now visible as status/cycle/action
controls, but not as complete form-based workflows:

```text
CMD_GET_DEVICE_TIME
CMD_SET_DEVICE_TIME
CMD_SEND_SELF_ADVERT
CMD_SET_ADVERT_NAME
CMD_SET_ADVERT_LATLON
CMD_ADD_UPDATE_CONTACT
CMD_REMOVE_CONTACT
CMD_SHARE_CONTACT
CMD_EXPORT_CONTACT
CMD_IMPORT_CONTACT
CMD_GET_CONTACT_BY_KEY
CMD_RESET_PATH
CMD_GET_CHANNEL
CMD_SET_CHANNEL
CMD_SET_RADIO_PARAMS
CMD_SET_TUNING_PARAMS
CMD_GET_TUNING_PARAMS
CMD_SET_OTHER_PARAMS
CMD_SET_AUTOADD_CONFIG
CMD_GET_AUTOADD_CONFIG
CMD_GET_ALLOWED_REPEAT_FREQ
CMD_SEND_TRACE_PATH
CMD_GET_ADVERT_PATH
CMD_GET_STATS
CMD_SEND_LOGIN
CMD_LOGOUT
CMD_SEND_STATUS_REQ
CMD_SEND_PATH_DISCOVERY_REQ
CMD_SEND_TELEMETRY_REQ
CMD_SEND_BINARY_REQ
CMD_SEND_ANON_REQ
CMD_SEND_RAW_DATA
CMD_SEND_CHANNEL_DATA
CMD_SEND_CONTROL_DATA
CMD_SET_DEVICE_PIN
CMD_SIGN_START
CMD_SIGN_DATA
CMD_SIGN_FINISH
CMD_EXPORT_PRIVATE_KEY
CMD_IMPORT_PRIVATE_KEY
CMD_FACTORY_RESET
CMD_REBOOT
CMD_SET_FLOOD_SCOPE_KEY
CMD_SET_DEFAULT_FLOOD_SCOPE
CMD_GET_DEFAULT_FLOOD_SCOPE
CMD_GET_CUSTOM_VARS
CMD_SET_CUSTOM_VAR
```

Some of these are already partially handled over BLE by the firmware and some
now have GUI entry points. They remain listed when the T-Deck GUI cannot yet
edit all parameters or show the full response flow.

## Suggested screen plan

Keep the Windows CE desktop model and add these apps:

- Contacts: replaces overloading Nodes for contact management.
- Channels: promote channel editing into a full app instead of only Chat mode.
- Radio Advanced: numeric LoRa and tuning settings.
- Identity: node name, public key, advert policy, key/admin actions.
- BLE: companion connection and protocol diagnostics.
- Tools: trace/path discovery, self advert, stats, reboot.

Keep destructive operations behind an `Advanced` or `Admin` mode so normal
field use stays fast and hard to break.
