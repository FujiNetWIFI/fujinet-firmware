# lib/bus/rc2014sio

RC2014 bus over a 115200 baud serial link for `BUILD_RC2014` with `RC2014_BUS_SIO`.

## Layout
| File | Defines |
|---|---|
| `rc2014sio.h`, `rc2014sio.cpp` | its own `virtualDevice` (legacy `rc2014_process(commanddata, checksum)`) and a `systemBus` that does not derive `SystemBusBase` |

## How it fits
- Unreachable: [lib/bus/bus.h](../bus.h) only includes [lib/bus/rc2014bus/](../rc2014bus/) for
  `BUILD_RC2014`, and no board ini defines `RC2014_BUS_SIO`. Devices live in
  [lib/device/rc2014/](../../device/rc2014/).

## Build
ESP only. Legacy: it writes to `fnUartBUS`, which no longer exists, and defines no
`FUJI_COMMAND_PACKET` (#1658). Not compiled on PC.

## Notes
Shares the include guard `rc2014_H` with [rc2014bus.h](../rc2014bus/rc2014bus.h).
