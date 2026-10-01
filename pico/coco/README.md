# pico/coco

An early RP2040 cartridge for the Tandy Color Computer that presents a DriveWire ROM on the
cartridge bus and forwards the serial traffic to the ESP32.

## Layout
| File | Defines |
|---|---|
| `main.c` | Cartridge bus emulation and the PIO UART bridge to the ESP32 |
| `cococart.pio` | PIO state machines that answer the CoCo's ROM reads |
| `uart_tx.pio`, `uart_rx.pio` | PIO UART toward the ESP32 |
| `rom.c`, `rom.h`, `roms/` | The HDB-DOS and Becker ROM images served to the CoCo |
| `CMakeLists.txt`, `pico_sdk_import.cmake` | pico-sdk project `main`; `PICO_BOARD` defaults to `pico`; USB stdio |

## How it fits
- Pairs with the DriveWire bus in [lib/bus/drivewire/](../../lib/bus/drivewire/).
  `include/pinmap/coco_cart.h` holds the matching ESP32 pin assignment, which no board ini selects.
- Superseded by the `fujiversal-drivewire` board, whose cartridge firmware is the
  [fujiversal/](../fujiversal/) submodule.

## Build
Standalone pico-sdk CMake; not embedded by `build_pico.py`. Configure a `build` directory here
first, then `make pico-de-coco` at the repository root runs it.
