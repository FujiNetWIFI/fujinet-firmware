# lib/bus/iec

Commodore serial IEC bus for `BUILD_IEC`, built on David Hansel's IECDevice library with the
fast-loader options selected in `IECConfig.h`. An ESP32 drives the bus lines. With `[BOIP]` enabled,
on any platform, the bus instead takes transactions from an emulator over NetIEC; FujiNet-PC has no
lines, so that is how it is used.

## How it fits
- Selected by `BUILD_IEC` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/iec/](../../device/iec/) and are registered with the library's `attachDevice()` rather
  than `addDevice()`.
- Disk images are opened through [lib/meatloaf/](../../meatloaf/), not through
  [lib/media/cbm/](../../media/cbm/).

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_IEC`. PC: `FUJINET_TARGET=IEC` lists
every file except `IECespidf.h`. On any platform with `[BOIP]` enabled, `systemBus` listens for NetIEC
on the `[BOIP]` port (default 1541) through `BoIPChannel` and tells `IECBusHandler` to leave the bus
lines alone; VICE reaches it through an OpenCBM-ABI bridge library.

## Notes
`rotateDevices()` is an empty stub kept for `fujiDevice`. Code reachable from the bus ISR must be
`IRAM_ATTR`; `IECBusHandler.cpp` refuses to build without it.
- `IECBusHandler.*`, `IECDevice.*`, `IECFileDevice.*` and `IECConfig.h` are the vendored IECDevice
  library.
- After `useNetIEC()`, `IECBusHandler` leaves the bus lines alone.
