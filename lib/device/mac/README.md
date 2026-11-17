# lib/device/mac

The Macintosh 68k device set for `BUILD_MAC`. ESP32 only.

## How it fits
- Devices derive the `virtualDevice` (alias `macDevice`) of [lib/bus/mac/](../../bus/mac/). Images
  and the GCR encoder come from [lib/media/mac/](../../media/mac/), archive decoding from
  [lib/stuffit/](../../stuffit/).
- Mounting is driven from the web UI; there is no CONFIG program and no clock or network device.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_MAC`. Not compiled on PC.

## Notes
`floppy.h`, `modem.h` and `macFuji.h` carry `#if 0` blocks of the Apple II code they were copied
from.
- `macFuji::process()` is empty because no Fuji commands reach this bus.
