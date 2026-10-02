# lib/bus/rs232

Generic serial FujiNet bus for `BUILD_RS232`: SLIP-framed `FujiBusPacket` frames at 115200 baud
over UART, USB CDC-ACM or BoIP. It serves the PC/MS-DOS RS-232 boards and every `fujiversal-*`
board, where the RP2xxx cartridge is the other end of the link.

## Layout
| File | Defines |
|---|---|
| `rs232.h`, `rs232.cpp` | `virtualDevice` (devices implement `rs232_process()`) and `systemBus` (`readBusPacket()`, `writeBusPacket()`, `sendReplyPacket()`, `sendCommand()`, a per-unit network map; `nativeEOL()` is CR LF) |
| `FujiBusPacket.h`, `FujiBusPacket.cpp` | `FujiBusPacket`: SLIP encode and decode, device ID, command, `PacketParam` list and optional data; this bus's `FUJI_COMMAND_PACKET` |

## How it fits
- Selected by `BUILD_RS232` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/rs232/](../../device/rs232/).
- `FujiBusPacket` is also the packet type of [lib/bus/mac/](../mac/) and of the fujiversal path in
  [lib/bus/drivewire/](../drivewire/); the cartridge firmware under [pico/](../../../pico/) carries
  its own copy.
- Ports come from [lib/hardware/](../../hardware/): `UARTChannel`, `ACMChannel` (USB host on
  fujiversal boards) or `BoIPChannel` (PC).

## Build
ESP: globbed by `src/CMakeLists.txt`; `rs232.cpp` compiles only under `BUILD_RS232`, while
`FujiBusPacket.cpp` has no guard and is built into every target. PC: `FUJINET_TARGET=RS232` lists
both pairs; `tests/FujiBusPacketTests.cpp` unit-tests the framing.
