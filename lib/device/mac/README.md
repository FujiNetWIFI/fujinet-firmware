# lib/device/mac

The Macintosh 68k device set for `BUILD_MAC`. ESP32 only.

## Layout
| File | Defines |
|---|---|
| `macFuji.h`, `macFuji.cpp` | `macFuji : fujiDevice` (`.dsk` images); defines `platformFuji` and sets `theFuji`; `process()` is empty because no Fuji commands reach this bus |
| `floppy.h`, `floppy.cpp` | `macFloppy`, the `DISK_DEVICE`: one Mac disk slot that behaves as an HD20 (DCD) hard disk in the DCD slots or as the 800K GCR floppy in the floppy slot, and can mount a StuffIt archive through `SitMount` |
| `modem.h`, `modem.cpp` | `macModem : macDevice` |
| `printer.h`, `printer.cpp`, `printerlist.h`, `printerlist.cpp` | `macPrinter : macDevice` and `printerlist`; defines `fnPrinters` |

## How it fits
- Devices derive the `virtualDevice` (alias `macDevice`) of [lib/bus/mac/](../../bus/mac/). Images
  and the GCR encoder come from [lib/media/mac/](../../media/mac/), archive decoding from
  [lib/stuffit/](../../stuffit/).
- `macFuji` overrides `setup()`, `set_additional_direntry_details()` and `process()`. Mounting is
  driven from the web UI; there is no CONFIG program and no clock or network device.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_MAC`. Not compiled on PC.

## Notes
`floppy.h`, `modem.h` and `macFuji.h` carry `#if 0` blocks of the Apple II code they were copied
from.
