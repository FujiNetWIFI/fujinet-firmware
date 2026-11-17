# lib/bus/iwm

Apple II SmartPort and Disk II bus for `BUILD_APPLE`. On ESP32 it drives the IWM signals directly;
on the PC build it carries SmartPort over SLIP to an emulator.

## How it fits
- Selected by `BUILD_APPLE` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/iwm/](../../device/iwm/).
- The SLIP path decodes requests with `Request::from_packet()` from [lib/devrelay/](../../devrelay/)
  and connects to the host named by `Config.get_boip_host()`.

## Build
ESP: globbed by `src/CMakeLists.txt`; every file compiles only under `BUILD_APPLE`. PC:
`FUJINET_TARGET=APPLE` lists `iwm`, `iwm_slip`, `FujiIWMPacket`, `IWMBusIDMap` and one connector
chosen by the CMake option `SLIP_PROTOCOL` (`NET` or `COM`); `iwm_ll` and `spi_continuous` are ESP
only.

## Notes
`IWMBusIDMap.cpp` has no build guard and is compiled into every ESP target.
