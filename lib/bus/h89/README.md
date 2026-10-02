# lib/bus/h89

Heathkit H89 bus skeleton for `BUILD_H89`.

## Layout
| File | Defines |
|---|---|
| `h89.h`, `h89.cpp` | its own `virtualDevice` (legacy `process(commanddata, checksum)`) and a `systemBus` keeping a `std::map` of devices; it does not derive `SystemBusBase` |

## How it fits
- Selected by `BUILD_H89` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/h89/](../../device/h89/).

## Build
ESP only, `BUILD_H89`. Legacy: it defines no `FUJI_COMMAND_PACKET`, which the shared `fujiDevice`
requires, and the board fails to build (#1658). Not compiled on PC.
