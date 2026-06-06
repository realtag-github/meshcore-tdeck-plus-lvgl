#pragma once

// Pin map from LILYGO T-Deck examples/UnitTest/utilities.h.
// Keep all pin definitions here so driver code stays portable.

#define PIN_POWERON 10

#define PIN_I2C_SDA 18
#define PIN_I2C_SCL 8

#define PIN_DISPLAY_MOSI 41
#define PIN_DISPLAY_MISO 38
#define PIN_DISPLAY_SCLK 40
#define PIN_DISPLAY_CS   12
#define PIN_DISPLAY_DC   11
#define PIN_DISPLAY_RST  -1
#define PIN_DISPLAY_BL   42
#define PIN_TOUCH_INT    16

#define PIN_LORA_SCK  40
#define PIN_LORA_MISO 38
#define PIN_LORA_MOSI 41
#define PIN_LORA_CS   9
#define PIN_LORA_RST  17
#define PIN_LORA_BUSY 13
#define PIN_LORA_DIO1 45

// Matches the upstream MeshCore LilyGo T-Deck variant.
#define BOARD_LORA_DIO2_AS_RF_SWITCH 0
#define BOARD_LORA_TCXO_VOLTAGE 1.8f

#define PIN_BATTERY_ADC 4

#define PIN_GPS_RX 44
#define PIN_GPS_TX 43

#define PIN_SDCARD_MISO 38
#define PIN_SDCARD_MOSI 41
#define PIN_SDCARD_SCLK 40
#define PIN_SDCARD_CS   39

#define PIN_KEYBOARD_INT 46
#define PIN_TRACKBALL_UP 2
#define PIN_TRACKBALL_DOWN 3
#define PIN_TRACKBALL_LEFT 1
#define PIN_TRACKBALL_RIGHT 15
#define PIN_TRACKBALL_PRESS -1

#define PIN_I2S_WS 5
#define PIN_I2S_BCK 7
#define PIN_I2S_DOUT 6
#define PIN_SPEAKER PIN_I2S_DOUT
