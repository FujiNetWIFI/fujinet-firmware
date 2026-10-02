# lib/bus/sio

Atari SIO serial bus for `BUILD_ATARI`: command-frame reception, the ACK/NAK/COMPLETE/ERROR
handshake, high-speed (HSIO) baud switching and the NetSIO link to emulators.

## Layout
| File | Defines |
|---|---|
| `sio.h`, `sio.cpp` | `virtualDevice` (devices implement `sio_status()` and `sio_process()`) and `systemBus`, which owns the serial port, the HSIO index and baud switching, the NetSIO port, and cached pointers to the modem, Fuji, network, stream, cassette, CP/M and printer devices |
| `FujiSIOPacket.h`, `FujiSIOPacket.cpp` | `FujiSIOPacket`, this bus's `FUJI_COMMAND_PACKET` |
| `NetSIO.h`, `NetSIO.cpp` | `NetSIO`, an `IOChannel` that carries SIO over UDP to a NetSIO hub |

## How it fits
- Selected by `BUILD_ATARI` in [lib/bus/bus.h](../bus.h); the `SYSTEM_BUS` global defined in
  [src/](../../../src/) is this `systemBus`.
- Devices live in [lib/device/sio/](../../device/sio/). The serial transport is `UARTChannel` from
  [lib/hardware/](../../hardware/); the PC build switches the port to `NetSIO` to reach an emulator.

## Build
ESP: the `.cpp` files are globbed by `src/CMakeLists.txt` and compile only under `BUILD_ATARI`.
PC: `FUJINET_TARGET=ATARI` lists every file here.

## Notes
`systemBus` exposes raw `read()` and `write()` on the port for devices that stream outside the
command protocol, such as the cassette and the MIDI netstream.
