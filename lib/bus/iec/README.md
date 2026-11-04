# lib/bus/iec

Commodore serial IEC bus for `BUILD_IEC`, built on David Hansel's IECDevice library with the
fast-loader options selected in `IECConfig.h`. On ESP32 it drives the bus lines; on FujiNet-PC it
takes bus transactions from an emulator over NetIEC.

## Layout
| File | Defines |
|---|---|
| `iec.h`, `iec.cpp` | `systemBus`, deriving both `IECBusHandler` and `SystemBusBase`; PETSCII conversion in `nativeTextToUnicode()` and `unicodeTextToNative()`; the bus device numbers `BUS_DEVICEID_PRINTER`, `BUS_DEVICEID_DISK` and `BUS_DEVICEID_NETWORK` |
| `virtualDevice.h`, `virtualDevice.cpp` | `virtualDevice : IECDevice`; Fuji-style devices implement `processCommand()` |
| `FujiIECPacket.h`, `FujiIECPacket.cpp` | `FujiIECPacket`, this bus's `FUJI_COMMAND_PACKET` |
| `IECBusHandler.h`, `IECBusHandler.cpp`, `IECDevice.h`, `IECDevice.cpp`, `IECFileDevice.h`, `IECFileDevice.cpp`, `IECConfig.h` | the vendored IECDevice library: bus timing plus the `IECDevice` and `IECFileDevice` base classes |
| `IECespidf.h` | Arduino-compatibility shims the library needs on ESP-IDF |
| `IEChost.h` | the same shims for host builds |
| `IECBusHandlerHost.cpp` | `IECBusHandler` for host builds: no bus lines, transactions arrive already decoded through `atnCommand()`, `listenerWrite()` and `talkerRead()` |
| `NetIEC.h`, `NetIEC.cpp` | `NetIEC`, the IEC-over-TCP protocol: framed ATN, WRITE, READ and RESET requests from an emulator's bus master, replied to once each |

## How it fits
- Selected by `BUILD_IEC` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/iec/](../../device/iec/) and are registered with the library's `attachDevice()` rather
  than `addDevice()`.
- Disk images are opened through [lib/meatloaf/](../../meatloaf/), not through
  [lib/media/cbm/](../../media/cbm/).

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_IEC`; `IECBusHandlerHost.cpp` and
`NetIEC.cpp` compile to nothing there. PC: `FUJINET_TARGET=IEC` lists every file except
`IECBusHandler.cpp` and `IECespidf.h`. `systemBus` listens for NetIEC on the `[BOIP]` port (default 1541)
through `BoIPChannel`; VICE reaches it through an OpenCBM-ABI bridge library.

## Notes
`rotateDevices()` is an empty stub kept for `fujiDevice`. Code reachable from the bus ISR must be
`IRAM_ATTR`; `IECBusHandler.cpp` refuses to build without it.
