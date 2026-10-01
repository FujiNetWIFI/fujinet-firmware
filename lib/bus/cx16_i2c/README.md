# lib/bus/cx16_i2c

Commander X16 bus for `BUILD_CX16`: the ESP32 acts as an I2C slave at `I2C_DEVICE_ID` with the
register map described at the top of `cx16_i2c.h`.

## Layout
| File | Defines |
|---|---|
| `cx16_i2c.h`, `cx16_i2c.cpp` | its own `virtualDevice` (legacy `process(commanddata, checksum)` and `status()`) and a `systemBus` that does not derive `SystemBusBase` |

## How it fits
- Selected by `BUILD_CX16` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/cx16_i2c/](../../device/cx16_i2c/).

## Build
ESP only, `BUILD_CX16`. Legacy: it defines no `FUJI_COMMAND_PACKET`, which the shared `fujiDevice`
requires, and the board fails to build (#1658). Not compiled on PC.
