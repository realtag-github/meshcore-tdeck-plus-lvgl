# Driver layer

Keep hardware-specific code here.

Recommended files:

- display.cpp
- touch.cpp
- keyboard.cpp
- trackball.cpp
- gps.cpp
- battery.cpp
- sdcard.cpp
- audio.cpp
- radio.cpp

Each driver should expose a small service-like API and avoid direct UI calls.

Current implementation:

- `hardware_services.h/.cpp`

Implemented services:

- `BatteryService`: polls battery percentage when `PIN_BATTERY_ADC` is known,
  otherwise reports invalid status.
- `GpsService`: reads NMEA sentences from the configured hardware UART when GPS
  is enabled. GPS is disabled by default.
- `StorageService`: mounts SD, appends field logs, appends message-history and
  node-history records, and reads the most recent records at boot.
- `InputService`: reads the keyboard controller and active-low trackball GPIOs
  into the firmware input queue.
- `RadioService`: initializes the SX1262 through RadioLib, applies regional
  frequency and TX-power changes, transmits direct frames, and polls for raw
  received frames. If the hardware radio does not initialize, sends fail instead
  of accepting a fake transmit.
- `AudioService`: short `tone()` notifications for queued sends and received
  packets when audio is enabled.
