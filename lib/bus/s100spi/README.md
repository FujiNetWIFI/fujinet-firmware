# lib/bus/s100spi

S-100 bus over SPI for `BUILD_S100`.

## Layout
| File | Defines |
|---|---|
| `s100spi.h`, `s100spi.cpp` | its own `virtualDevice` and device-ID constants, and a `systemBus` keeping a `std::map` of devices; it does not derive `SystemBusBase` |

## How it fits
- Selected by `BUILD_S100` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/s100spi/](../../device/s100spi/). The S100 block in `src/main.cpp` is commented out,
  and `lib/media/media.h` gives this platform the ADAM media types.

## Build
ESP only, `BUILD_S100`. Legacy: it defines no `FUJI_COMMAND_PACKET`, which the shared `fujiDevice`
requires (#1658). Not compiled on PC.
