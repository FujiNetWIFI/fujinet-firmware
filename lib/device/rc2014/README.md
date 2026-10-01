# lib/device/rc2014

The RC2014 device set for `BUILD_RC2014`. Legacy: the bus defines no `FUJI_COMMAND_PACKET`, which
`fujiDevice` requires (#1658).

## Layout
| File | Defines |
|---|---|
| `rc2014Fuji.h`, `rc2014Fuji.cpp` | `rc2014Fuji : fujiDevice` (`.img` images) with legacy `rc2014_process()` overrides; defines `platformFuji` and sets `theFuji` |
| `disk.h`, `disk.cpp` | `rc2014Disk`, the `DISK_DEVICE` |
| `rc2014Network.h`, `rc2014Network.cpp` | `rc2014Network : virtualDevice`, a standalone network device that predates `NDevice` and still uses `ProtocolParser`, which no longer exists |
| `modem.h`, `modem.cpp` | `rc2014Modem` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `rc2014Printer` and `printerlist`; defines `fnPrinters` |
| `rc2014cpm.h`, `rc2014cpm.cpp` | `rc2014CPM`, CP/M through [lib/runcpm/](../../runcpm/) |

## How it fits
- Devices implement `rc2014_process(commanddata, checksum)` from
  [lib/bus/rc2014bus/](../../bus/rc2014bus/) (or the unreachable
  [lib/bus/rc2014sio/](../../bus/rc2014sio/)); this directory's name matches neither bus directory.
  Media comes from [lib/media/rc2014/](../../media/rc2014/).

## Build
ESP only, `BUILD_RC2014`; not compiled on PC.
