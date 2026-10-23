# lib/bus/rs232

Generic serial FujiNet bus for `BUILD_RS232`: SLIP-framed `FujiBusPacket` frames at 115200 baud
over UART, USB CDC-ACM, an 82C55 or BoIP. It serves the PC/MS-DOS RS-232 boards, the H89 card and
every `fujiversal-*` board, where the RP2xxx cartridge is the other end of the link.

## Layout
| File | Defines |
|---|---|
| `rs232.h`, `rs232.cpp` | `virtualDevice` (devices implement `rs232_process()`) and `systemBus` (the `readFujiBusPacket()`/`writeFujiBusPacket()` overrides, `sendReplyPacket()`, a per-unit network map; `nativeEOL()` is CR LF) |

## How it fits
- Selected by `BUILD_RS232` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/rs232/](../../device/rs232/).
- Packets are `FujiBusPacket` from [lib/bus/](../FujiBusPacket.h), shared with
  [lib/bus/mac/](../mac/) and the fujiversal path in [lib/bus/drivewire/](../drivewire/); the
  cartridge firmware under [pico/](../../../pico/) carries its own copy.
- Ports come from [lib/hardware/](../../hardware/): `UARTChannel`, `ACMChannel` (USB host on
  fujiversal boards), `PPIChannel` (the H89 card, chosen when the pinmap defines `PIN_PPI_OBF`) or
  `BoIPChannel` (PC).

## Build
ESP: globbed by `src/CMakeLists.txt`; `rs232.cpp` compiles only under `BUILD_RS232`. PC:
`FUJINET_TARGET=RS232` lists it.
