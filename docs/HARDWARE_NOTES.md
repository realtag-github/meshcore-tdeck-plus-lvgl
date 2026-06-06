# Hardware notes

Target board: LILYGO T-Deck Plus.

Expected display target: 320 x 240 landscape, 4:3.

Key peripherals:

- ESP32-S3
- SX1262 LoRa radio
- ST7789-class 320 x 240 display
- touch input
- keyboard
- trackball/navigation input
- GPS
- microSD
- speaker
- battery ADC

## Pin source

The current `firmware/include/board_pins.h` values are copied from LILYGO's
official `Xinyuan-LilyGO/T-Deck` `examples/UnitTest/utilities.h` pin map.

Relevant upstream facts:

- `BOARD_POWERON` is GPIO 10 and must be driven high for peripherals.
- Shared SPI is SCK 40, MISO 38, MOSI 41.
- TFT CS/DC/backlight are 12/11/42.
- SX1262 uses CS 9, BUSY 13, RST 17, DIO1 45.
- Battery ADC is GPIO 4.
- Touch interrupt is GPIO 16.
- Keyboard interrupt is GPIO 46.
- SD card CS is GPIO 39.
- T-Deck Plus GPS is TX 43, RX 44.

Source:

```text
https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/utilities.h
```

## Implemented hardware paths

- Board power GPIO 10 is enabled at service startup.
- Display backlight GPIO 42 is enabled at service startup.
- LVGL flushes through TFT_eSPI to the ST7789 panel.
- GT911 touch is read directly over I2C using addresses `0x5d` then `0x14`.
- Keyboard bytes are read from I2C address `0x55`.
- Trackball direction pins are sampled as active-low GPIO inputs.
- GPS NMEA is read from `Serial1` on RX 44 / TX 43.
- SD card is mounted through Arduino `SD` over the shared SPI bus.
- Field logs are appended to `/meshcore.log`.
- Message history is appended to `/meshcore_messages.log` and restored at boot
  when the SD card is readable.
- Deleting a message appends a tombstone record to the same history file.
- Node/contact history is appended to `/meshcore_nodes.log` and restored at boot;
  real radio RX sender IDs update the node list.
- Test LoRa direct frames are sent as `MC1|sender|target|body`; inbound legacy
  `sender:body` and raw payloads still render in the inbox for bring-up.
- Structured inbound frames for other targets are ignored; broadcast targets are
  empty, `*`, `all`, or `broadcast`.
- Position frames use `POS1|sender|target|lat|lon`; accepted positions update
  the node list and Map screen.

## Still requiring hardware validation

- Display initialization sequence for the exact installed ST7789 panel.
- Touch controller bring-up and coordinate transform.
- Keyboard I2C protocol and key map.
- Trackball direction mapping for GPIO 1/2/3/15.
- SD card first-boot behavior on the shared SPI bus.
- GPS baud rate and antenna/fix behavior.
- SX1262 reset/busy/DIO1 timing with the real MeshCore radio backend.

## Bring-up commands

Build and flash a regional firmware:

```bash
tools/flash_tdeck.sh tdeck-plus-915
```

Open the serial console:

```bash
tools/monitor_tdeck.sh
```

Useful serial commands after boot:

```text
help
status
logs
messages
nodes
clear-messages
clear-nodes
name FieldDeck
node 0xA71B
rename-node 0xA71B Alpha-7
region
power 20
path
gps
audio
room
admin
inject hello from serial
inject-frame MC1|0xBEEF|0xA71B|hello structured frame
inject-frame POS1|0xBEEF|*|37.776000|-122.421000
send 0xA71B hello
send-pos *
```
