# lib/bus/adamnet

Coleco ADAM AdamNet bus for `BUILD_ADAM`: the one-wire 62.5 kbaud serial protocol on ESP32, or
Bus-over-IP (BoIP) to an emulator on the PC build.

## How it fits
- Selected by `BUILD_ADAM` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/adamnet/](../../device/adamnet/).
- On ESP32 `start_bus_task()` runs the bus on its own FreeRTOS task pinned to one core and
  `src/main.cpp` skips `SYSTEM_BUS.service()`; on PC the main loop calls `service()` and the port is
  a `BoIPChannel` from [lib/hardware/](../../hardware/).

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_ADAM`. PC: `FUJINET_TARGET=ADAM`
lists every file here.
