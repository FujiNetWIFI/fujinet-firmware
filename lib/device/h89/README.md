# lib/device/h89

The Heathkit H89 device set for `BUILD_H89`. Legacy: fails to build (#1658).

## Layout
| File | Defines |
|---|---|
| `H89Fuji.h`, `H89Fuji.cpp` | `H89Fuji : virtualDevice`, the design that predates `fujiDevice`; defines `platformFuji` and an `H89Fuji *theFuji` |
| `disk.h`, `disk.cpp` | `H89Disk`, the `DISK_DEVICE` |
| `H89Network.h`, `H89Network.cpp` | `H89Network : virtualDevice`, a standalone network device that predates `NDevice` and still uses `ProtocolParser`, which no longer exists |
| `modem.h`, `modem.cpp` | `H89Modem` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `H89Printer` and `printerlist`; defines `fnPrinters` |

## How it fits
- Devices implement the legacy `process(commanddata, checksum)` of [lib/bus/h89/](../../bus/h89/).
  Media comes from [lib/media/h89/](../../media/h89/).

## Build
ESP only, `BUILD_H89`; not compiled on PC.
