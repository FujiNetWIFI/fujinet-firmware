# lib/bus/comlynx

Atari Lynx ComLynx bus for `BUILD_LYNX`: 62.5 kbaud serial over UART on ESP32 or BoIP on the PC
build, plus the UDP netstream and RedEye game-link relay.

## Layout
| File | Defines |
|---|---|
| `comlynx.h`, `comlynx.cpp` | `virtualDevice` (devices override `comlynx_process()` and `reset()`) and `systemBus` (command queue, ACK/NAK packets, `change_baud()`, `setStreamHost()`, `setRedeyeMode()`, `setRedeyeGameRemap()`, raw port passthroughs) |
| `FujiLynxPacket.h`, `FujiLynxPacket.cpp` | `FujiLynxPacket`, this bus's `FUJI_COMMAND_PACKET` |

## How it fits
- Selected by `BUILD_LYNX` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/comlynx/](../../device/comlynx/), where `lynxNetStream` and
  [redeye.cpp](../../device/comlynx/redeye.cpp) implement the stream and RedEye features this bus
  switches on.
- The port is `UARTChannel` on ESP32 and `BoIPChannel` on PC, both from
  [lib/hardware/](../../hardware/).

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_LYNX`. PC: `FUJINET_TARGET=LYNX`
lists every file here and adds this directory to the include path because the headers use bare
names.
