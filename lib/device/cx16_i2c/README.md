# lib/device/cx16_i2c

The Commander X16 device set for `BUILD_CX16`. Legacy: fails to build (#1658).

## Layout
| File | Defines |
|---|---|
| `cx16Fuji.h`, `cx16Fuji.cpp` | `cx16Fuji : virtualDevice`, the design that predates `fujiDevice`; defines `platformFuji` and a `cx16Fuji *theFuji` |
| `disk.h`, `disk.cpp` | `cx16Disk`, the `DISK_DEVICE` |
| `modem.h`, `modem.cpp` | `cx16Modem` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `cx16Printer` and `printerlist`; defines `fnPrinters` |

## How it fits
- Devices implement the legacy `process(commanddata, checksum)` of
  [lib/bus/cx16_i2c/](../../bus/cx16_i2c/). No network device is wired in; `lib/device/device.h`
  comments it out. Media comes from [lib/media/cx16/](../../media/cx16/).

## Build
ESP only, `BUILD_CX16`; not compiled on PC.
