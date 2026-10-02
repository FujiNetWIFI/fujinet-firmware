# lib/bus/adamnet

Coleco ADAM AdamNet bus for `BUILD_ADAM`: the one-wire 62.5 kbaud serial protocol on ESP32, or
Bus-over-IP (BoIP) to an emulator on the PC build.

## Layout
| File | Defines |
|---|---|
| `adamnet.h`, `adamnet.cpp` | `virtualDevice` (devices implement `adamnet_control_send()` and `deviceStatus()`, and may override `adamnet_control_ready()`, `adamnet_control_receive()`, `adamnet_idle()` and `reset()`) and `systemBus` (command dispatch, ACK/NAK reply packets, `deviceExists()`, `deviceEnabled()`, `start_bus_task()`) |
| `AdamNetPhase.h`, `AdamNetPhase.cpp` | `AdamNetPhase`, the per-transaction phase state machine |
| `FujiAdamPacket.h`, `FujiAdamPacket.cpp` | `FujiAdamPacket`, this bus's `FUJI_COMMAND_PACKET`; parameters are big-endian and read through `PacketParamProxy` |

## How it fits
- Selected by `BUILD_ADAM` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/adamnet/](../../device/adamnet/).
- On ESP32 `start_bus_task()` runs the bus on its own FreeRTOS task pinned to one core and
  `src/main.cpp` skips `SYSTEM_BUS.service()`; on PC the main loop calls `service()` and the port is
  a `BoIPChannel` from [lib/hardware/](../../hardware/).

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_ADAM`. PC: `FUJINET_TARGET=ADAM`
lists every file here.
