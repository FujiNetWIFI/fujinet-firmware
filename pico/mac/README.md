# pico/mac

RP2040 firmware for the `fujimac-rev0` board. It sits on the Macintosh DB-19 floppy port and
emulates the drive side of an 800K GCR floppy and of HD20 hard disks in PIO state machines.

## How it fits
- The ESP32 side is [lib/bus/mac/](../../lib/bus/mac/). The two chips share a 2 Mbaud UART with a
  one-character command protocol documented at the top of `lib/bus/mac/mac.h`. Floppy read data is
  streamed by the ESP32 RMT peripheral, not over the UART.
- `docs/mac68k.md` describes the board and its disk slots; `docs/mac68k-floppy-write.md` the write
  path.

## Build
Standalone pico-sdk CMake, flashed by hand; not embedded by `build_pico.py`.
