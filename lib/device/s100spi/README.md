# lib/device/s100spi

The S-100 device set for `BUILD_S100`. Legacy: the bus defines no `FUJI_COMMAND_PACKET` (#1658).

## Layout
| File | Defines |
|---|---|
| `s100spiFuji.h`, `s100spiFuji.cpp` | `s100spiFuji : virtualDevice`, the design that predates `fujiDevice`; defines `platformFuji` and an `s100spiFuji *theFuji` |
| `disk.h`, `disk.cpp` | `s100spiDisk`, the `DISK_DEVICE` |
| `s100spiNetwork.h`, `s100spiNetwork.cpp` | `s100spiNetwork : virtualDevice`, a standalone network device that predates `NDevice` and still uses `ProtocolParser`, which no longer exists |
| `modem.h`, `modem.cpp` | `s100spiModem` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `s100spiPrinter` and `printerlist`; defines `fnPrinters` |

## How it fits
- Devices implement the legacy interface of [lib/bus/s100spi/](../../bus/s100spi/).
  `lib/media/media.h` gives this platform the ADAM media types from
  [lib/media/adam/](../../media/adam/), and the S100 setup block in `src/main.cpp` is commented out.

## Build
ESP only, `BUILD_S100`; not compiled on PC.
