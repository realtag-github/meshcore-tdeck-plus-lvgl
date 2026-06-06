# Screen specification

## Home

Purpose: quick status and entry points.

Content:

- connection status
- channel
- node count
- battery
- last message preview

Actions:

- Messages
- Nodes
- Map
- Menu

## Inbox

Purpose: show latest conversations.

Rows:

- icon
- sender
- subject/preview
- time

Actions:

- New
- Reply
- Delete
- More

## Message view

Purpose: read one received message.

Content:

- sender
- recipient
- time
- message body

Actions:

- Reply
- Forward
- Delete
- Back

## Chat compose

Purpose: keyboard-first message composition.

Content:

- recipient
- recent bubbles
- text input

Actions:

- Send
- Attach location later
- Back

## Nodes

Purpose: show nearby known nodes.

Rows:

- node name
- short ID
- RSSI
- SNR
- last seen

Actions:

- Refresh
- Ping
- Details
- Map

## Channels

Purpose: choose active channel and view occupancy.

Rows:

- channel name
- current indicator
- users count

Actions:

- Join
- Leave
- Details
- New

## Map

Purpose: simple situational view.

MVP can use a simplified grid map before offline tiles.

Actions:

- Center
- Layers
- Zoom in
- Zoom out

## Settings

Purpose: category list, not dense forms.

Rows:

- Radio
- Network
- Device
- Display
- Sounds
- GPS
- Power
- About

Actions:

- Home
- Radio
- Server
- GPS toggle

## Radio

Purpose: expose MeshCore radio settings that must be visible before hardware
integration.

Rows:

- region preset
- TX power
- path-hash mode
- radio state

Actions:

- Back
- Region
- Power
- Path

## Servers

Purpose: expose room-server and repeater administration state.

Rows:

- room-server login state
- repeater remote-admin state
- clock-sync state
- device registration state

Actions:

- Back
- Room
- Admin
- Clock

## Diagnostics

Purpose: support field debugging.

Content:

- heap
- PSRAM
- uptime
- radio state
- RSSI/SNR
- SD state
- GPS state
