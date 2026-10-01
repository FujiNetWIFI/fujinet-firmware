# lib/bus/rc2014bus

RC2014 bus over SPI for `BUILD_RC2014` with `RC2014_BUS_SPI`: a FIFO-buffered, interrupt-polled
transport.

## Layout
| File | Defines |
|---|---|
| `rc2014bus.h`, `rc2014bus.cpp` | `rc2014Fifo`, its own `virtualDevice` (legacy `rc2014_process(commanddata, checksum)`) and a `systemBus` that does not derive `SystemBusBase` |

## How it fits
- [lib/bus/bus.h](../bus.h) includes this header for `BUILD_RC2014`; the serial alternative is
  [lib/bus/rc2014sio/](../rc2014sio/). Devices live in [lib/device/rc2014/](../../device/rc2014/).

## Build
ESP only. Legacy: it defines no `FUJI_COMMAND_PACKET`, which the shared `fujiDevice` requires
(#1658). Not compiled on PC.

## Notes
Both RC2014 bus headers use the include guard `rc2014_H`.
