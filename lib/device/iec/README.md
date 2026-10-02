# lib/device/iec

The Commodore device set for `BUILD_IEC`. ESP32 only.

## Layout
| File | Defines |
|---|---|
| `iecFuji.h`, `iecFuji.cpp` | `iecFuji : fujiDevice` (`.d64` images); defines `platformFuji` and sets `theFuji`; `setup()` attaches the printer, the drives, the network, the clock and itself to the bus under their Commodore device numbers |
| `drive.h`, `drive.cpp` | `iecDrive : IECFileDevice`, the `DISK_DEVICE`: a Commodore drive whose channels (`iecChannelHandler`), directory and disk images come from [lib/meatloaf/](../../meatloaf/) |
| `iecNetwork.h`, `iecNetwork.cpp` | `iecNetwork : NDevice` |
| `iecClock.h`, `iecClock.cpp` | `iecClock : IECDevice`, a raw IEC clock device |
| `modem.h`, `modem.cpp` | `iecModem : IECDevice` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `iecPrinter : IECDevice` and `printerlist`; defines `fnPrinters` |
| `cpm.h`, `cpm.cpp` | `iecCpm`, an empty placeholder |
| `dos/_dos.h` | commented-out DOS tables, included by nothing |

## How it fits
- Three device bases coexist: `iecFuji` and `iecNetwork` derive the bus's `virtualDevice` (an
  `IECDevice` underneath) and implement `processCommand()`; `iecDrive` derives the library's
  `IECFileDevice`; the clock, modem and printer derive `IECDevice` directly and do not use
  `fujiClock`.
- `iecFuji` overrides `setup()`, `set_additional_direntry_details()` and `processCommand()`.
  `iecNetwork` overrides `fujidev_write()`. Devices are registered with `attachDevice()` from
  [lib/bus/iec/](../../bus/iec/), not `addDevice()`. [lib/media/cbm/](../../media/cbm/) only
  supplies the `mediatype_t` values; image handling is in Meatloaf.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_IEC`. Not compiled on PC.

## Notes
`drive.h` can target a `USE_VDRIVE` backend whose headers are not in this tree.
