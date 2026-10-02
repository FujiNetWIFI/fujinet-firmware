# pico/mac

RP2040 firmware for the `fujimac-rev0` board. It sits on the Macintosh DB-19 floppy port and
emulates the drive side of an 800K GCR floppy and of HD20 hard disks in PIO state machines.

## Layout
| File | Defines |
|---|---|
| `commands.c` | Main loop: PIO setup, the UART command protocol with the ESP32, floppy and DCD state |
| `commands.pio`, `latch.pio`, `mux.pio`, `enand.pio`, `echo.pio` | Floppy (MCI) signal decoding and the drive register latch |
| `dcd_commands.pio`, `dcd_read.pio`, `dcd_write.pio` | HD20 (DCD) command, read and write transfers |
| `gcr_capture.pio` | Captures GCR write data for the writable-floppy path |
| `CMakeLists.txt`, `pico_sdk_import.cmake` | pico-sdk project `commands`; `PICO_BOARD` defaults to `pico`; USB and UART stdio |

## How it fits
- The ESP32 side is [lib/bus/mac/](../../lib/bus/mac/). The two chips share a 2 Mbaud UART with a
  one-character command protocol documented at the top of `lib/bus/mac/mac.h`. Floppy read data is
  streamed by the ESP32 RMT peripheral, not over the UART.
- `docs/mac68k.md` describes the board and its disk slots; `docs/mac68k-floppy-write.md` the write
  path.

## Build
Standalone pico-sdk CMake, flashed by hand; not embedded by `build_pico.py`.
