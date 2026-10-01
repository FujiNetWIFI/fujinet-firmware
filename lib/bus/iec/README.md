# lib/bus/iec

Commodore serial IEC bus for `BUILD_IEC`, built on David Hansel's IECDevice library with the
fast-loader options selected in `IECConfig.h`. ESP32 only.

## Layout
| File | Defines |
|---|---|
| `iec.h`, `iec.cpp` | `systemBus`, deriving both `IECBusHandler` and `SystemBusBase`; PETSCII conversion in `nativeTextToUnicode()` and `unicodeTextToNative()`; the bus device numbers `BUS_DEVICEID_PRINTER`, `BUS_DEVICEID_DISK` and `BUS_DEVICEID_NETWORK` |
| `virtualDevice.h`, `virtualDevice.cpp` | `virtualDevice : IECDevice`; Fuji-style devices implement `processCommand()` |
| `FujiIECPacket.h`, `FujiIECPacket.cpp` | `FujiIECPacket`, this bus's `FUJI_COMMAND_PACKET` |
| `IECBusHandler.h`, `IECBusHandler.cpp`, `IECDevice.h`, `IECDevice.cpp`, `IECFileDevice.h`, `IECFileDevice.cpp`, `IECConfig.h` | the vendored IECDevice library: bus timing plus the `IECDevice` and `IECFileDevice` base classes |
| `IECespidf.h` | Arduino-compatibility shims the library needs on ESP-IDF |

## How it fits
- Selected by `BUILD_IEC` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/iec/](../../device/iec/) and are registered with the library's `attachDevice()` rather
  than `addDevice()`.
- Disk images are opened through [lib/meatloaf/](../../meatloaf/), not through
  [lib/media/cbm/](../../media/cbm/).

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_IEC`. Not compiled on PC.

## Notes
`rotateDevices()` is an empty stub kept for `fujiDevice`. Code reachable from the bus ISR must be
`IRAM_ATTR`; `IECBusHandler.cpp` refuses to build without it.
