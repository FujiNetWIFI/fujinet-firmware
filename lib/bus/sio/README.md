# lib/bus/sio

Atari SIO serial bus for `BUILD_ATARI`: command-frame reception, the ACK/NAK/COMPLETE/ERROR
handshake, high-speed (HSIO) baud switching and the NetSIO link to emulators.

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
